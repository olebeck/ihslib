#include "client_pri.h"
#include "pubkeys.h"
#include "ihslib/crypto.h"

#include <string.h>
#include <malloc.h>

typedef struct IHS_AuthorizationState {
    IHS_HostInfo host;
    char deviceName[64];
    char pin[16];
} IHS_AuthorizationState;

static void AuthorizationRequestTimer(uv_timer_t *handle, int status);

static void AuthorizationRequestCleanup(uv_handle_t *handle);

static void AuthorizationConfigureTicket(IHS_Client *client, IHS_AuthorizationState *state,
                                         CMsgRemoteDeviceAuthorizationRequest__CKeyEscrowTicket *ticket);

bool IHS_ClientAuthorizationRequest(IHS_Client *client, const IHS_HostInfo *host, const char *pin) {
    if (client->taskHandles.authorization) {
        return false;
    }
    uv_timer_t *timer = malloc(sizeof(uv_timer_t));
    uv_timer_init(client->loop, timer);
    timer->close_cb = AuthorizationRequestCleanup;
    IHS_AuthorizationState *state = malloc(sizeof(IHS_AuthorizationState));
    state->host = *host;
    strncpy(state->deviceName, client->deviceName, sizeof(state->deviceName) - 1);
    strncpy(state->pin, pin, sizeof(state->pin) - 1);
    timer->data = state;
    IHS_PRIV_ClientLock(client);
    client->taskHandles.authorization = timer;
    IHS_PRIV_ClientUnlock(client);
    uv_timer_start(timer, AuthorizationRequestTimer, 0, 1000);
    return true;
}


void IHS_PRIV_ClientAuthorizationCallback(IHS_Client *client, IHS_HostIP ip,
                                          CMsgRemoteClientBroadcastHeader *header, ProtobufCMessage *message) {
    uv_timer_t *timer = client->taskHandles.authorization;
    if (!timer) return;
    if (header->msg_type != k_ERemoteDeviceAuthorizationResponse) {
        return;
    }
    CMsgRemoteDeviceAuthorizationResponse *resp = (CMsgRemoteDeviceAuthorizationResponse *) message;
    switch (resp->result) {
        case k_ERemoteDeviceAuthorizationInProgress:
            if (client->callbacks.authorizationInProgress) {
                client->callbacks.authorizationInProgress(client);
            }
            return;
        case k_ERemoteDeviceAuthorizationSuccess:
            if (client->callbacks.authorizationSuccess) {
                client->callbacks.authorizationSuccess(client, resp->steamid);
            }
            break;
        default:
            if (client->callbacks.authorizationFailed) {
                client->callbacks.authorizationFailed(client, (IHS_AuthorizationResult) resp->result);
            }
            break;
    }
    uv_timer_stop(timer);
}


bool IHS_PRIV_ClientAuthorizationPubKey(IHS_Client *client, int euniverse, uint8_t *key, size_t *keyLen) {
    IHS_UNUSED(client);
    switch (euniverse) {
        case 1:
            if (*keyLen < sizeof(IHS_AuthorizationPubKey1)) return false;
            memcpy(key, IHS_AuthorizationPubKey1, sizeof(IHS_AuthorizationPubKey1));
            *keyLen = sizeof(IHS_AuthorizationPubKey1);
            break;
        case 2:
            if (*keyLen < sizeof(IHS_AuthorizationPubKey2)) return false;
            memcpy(key, IHS_AuthorizationPubKey2, sizeof(IHS_AuthorizationPubKey2));
            *keyLen = sizeof(IHS_AuthorizationPubKey2);
            break;
        case 3:
        case 4:
            if (*keyLen < sizeof(IHS_AuthorizationPubKey3And4)) return false;
            memcpy(key, IHS_AuthorizationPubKey3And4, sizeof(IHS_AuthorizationPubKey3And4));
            *keyLen = sizeof(IHS_AuthorizationPubKey3And4);
            break;
        default:
            return false;
    }
    return true;
}

static void AuthorizationRequestTimer(uv_timer_t *handle, int status) {
    IHS_Client *client = handle->loop->data;
    IHS_AuthorizationState *state = handle->data;
    uint8_t pubKey[384];
    size_t pubKeyLen = sizeof(pubKey);
    IHS_PRIV_ClientAuthorizationPubKey(client, state->host.euniverse, pubKey, &pubKeyLen);
    ProtobufCBinaryData deviceToken = {.data=client->deviceToken, .len = sizeof(client->deviceToken)};

    /* Initialize and serialize ticket */
    CMsgRemoteDeviceAuthorizationRequest__CKeyEscrowTicket ticket =
            CMSG_REMOTE_DEVICE_AUTHORIZATION_REQUEST__CKEY_ESCROW__TICKET__INIT;
    AuthorizationConfigureTicket(client, state, &ticket);
    uint8_t serTicket[2048];
    size_t serTicketLen = protobuf_c_message_pack(&ticket.base, serTicket);

    /* RSA encrypt ticket data */
    uint8_t encryptedTicket[2048];
    ProtobufCBinaryData encryptedRequest = {.data=encryptedTicket, .len = sizeof(encryptedTicket)};
    IHS_CryptoRSAEncrypt(serTicket, serTicketLen, pubKey, pubKeyLen, encryptedRequest.data,
                         &encryptedRequest.len);

    CMsgRemoteDeviceAuthorizationRequest request = CMSG_REMOTE_DEVICE_AUTHORIZATION_REQUEST__INIT;
    request.device_name = state->deviceName;
    request.device_token = deviceToken;
    request.encrypted_request = encryptedRequest;

    IHS_HostAddress address = state->host.address;
    IHS_PRIV_ClientSend(client, address, k_ERemoteDeviceAuthorizationRequest, (ProtobufCMessage *) &request);
}

static void AuthorizationConfigureTicket(IHS_Client *client, IHS_AuthorizationState *state,
                                         CMsgRemoteDeviceAuthorizationRequest__CKeyEscrowTicket *ticket) {
    ticket->has_password = true;
    ticket->password.len = strlen(state->pin);
    ticket->password.data = (uint8_t *) state->pin;

    ticket->has_identifier = true;
    ticket->identifier = client->deviceId;

    ticket->has_payload = true;
    ticket->payload.len = sizeof(client->secretKey);
    ticket->payload.data = client->secretKey;

    ticket->has_usage = true;
    ticket->usage = k_EKeyEscrowUsageStreamingDevice;

    ticket->device_name = client->deviceName;
}

static void AuthorizationRequestCleanup(uv_handle_t *handle) {
    IHS_Client *client = handle->loop->data;
    IHS_PRIV_ClientLock(client);
    client->taskHandles.authorization = NULL;
    IHS_PRIV_ClientUnlock(client);
    free(handle->data);
    free(handle);
}