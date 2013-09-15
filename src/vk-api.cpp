#include <debug.h>

#include "contrib/purple/http.h"

#include "vk-common.h"
#include "httputils.h"
#include "utils.h"

#include "vk-api.h"

namespace
{

// Callback, which is called upon receiving response to API call.
void on_vk_call_cb(PurpleHttpConnection* http_conn, PurpleHttpResponse* response, const CallSuccessCb& success_cb,
                   const CallErrorCb& error_cb);

}

void vk_call_api(PurpleConnection* gc, const char* method_name, const string_map& params, const CallSuccessCb& success_cb, const CallErrorCb& error_cb)
{
    VkConnData* conn_data = (VkConnData*)purple_connection_get_protocol_data(gc);

    string params_str = urlencode_form(params);
    string method_url = str_format("https://api.vk.com/method/%s?v=5.0&access_token=%s", method_name,
                                   conn_data->access_token().c_str());
    if (!params_str.empty()) {
        method_url += "&";
        method_url += params_str;
    }
    PurpleHttpRequest* req = purple_http_request_new(method_url.c_str());
    purple_http_request_set_method(req, "POST");
    http_request(gc, req, [=](PurpleHttpConnection* http_conn, PurpleHttpResponse* response) {
        // Connection has been cancelled due to account being disconnected. Do not do any response
        // processing, as callbacks may initiate new HTTP requests.
        if (conn_data->is_closing())
            return;

        on_vk_call_cb(http_conn, response, success_cb, error_cb);
    });
    purple_http_request_unref(req);
}

namespace
{

// Process error: maybe do another call and/or re-authorize.
void process_error(PurpleHttpConnection* http_conn, const picojson::value& error, const CallSuccessCb& success_cb,
                   const CallErrorCb& error_cb);
// Repeats API call
void repeat_vk_call(PurpleConnection* gc, PurpleHttpRequest* req, const CallSuccessCb& success_cb,
                    const CallErrorCb& error_cb);

void on_vk_call_cb(PurpleHttpConnection* http_conn, PurpleHttpResponse* response, const CallSuccessCb& success_cb,
                   const CallErrorCb& error_cb)
{
    if (!purple_http_response_is_successful(response)) {
        purple_debug_error("prpl-vkcom", "Error while calling API: %s\n", purple_http_response_get_error(response));
        if (error_cb)
            error_cb(picojson::value());
        return;
    }

    const char* response_text = purple_http_response_get_data(response, nullptr);
    picojson::value root;
    string error = picojson::parse(root, response_text, response_text + strlen(response_text));
    if (!error.empty()) {
        purple_debug_error("prpl-vkcom", "Error parsing %s: %s\n", response_text, error.c_str());
        if (error_cb)
            error_cb(picojson::value());
        return;
    }

    // Process all errors, potentially re-executing the request.
    if (root.contains("error")) {
        process_error(http_conn, root.get("error"), success_cb, error_cb);
        return;
    }

    if (!root.contains("response")) {
        purple_debug_error("prpl-vkcom", "Root element is neither \"response\" nor \"error\"\n");
        if (error_cb)
            error_cb(picojson::value());
        return;
    }

    success_cb(root.get("response"));
}

void process_error(PurpleHttpConnection* http_conn, const picojson::value& error, const CallSuccessCb& success_cb,
                   const CallErrorCb& error_cb)
{
    if (!error.is<picojson::object>()) {
        purple_debug_error("prpl-vkcom", "Unknown error response: %s\n", error.serialize().c_str());
        if (error_cb)
            error_cb(picojson::value());
        return;
    }

    if (!field_is_present<double>(error, "error_code")) {
        purple_debug_error("prpl-vkcom", "Unknown error response: %s\n", error.serialize().c_str());
        if (error_cb)
            error_cb(picojson::value());
        return;
    }

    int error_code = error.get("error_code").get<double>();
    if (error_code == VK_AUTHORIZATION_FAILED || error_code == VK_TOO_MANY_REQUESTS_PER_SECOND) {
        PurpleConnection* gc = purple_http_conn_get_purple_connection(http_conn);
        PurpleHttpRequest* req = purple_http_conn_get_request(http_conn);
        purple_http_request_ref(req); // Increment references, or the request will die with http_conn.

        if (error_code == VK_AUTHORIZATION_FAILED) {
            purple_debug_info("prpl-vkcom", "Access token expired, doing a reauthorization\n");

            VkConnData* data = (VkConnData*)purple_connection_get_protocol_data(gc);
            data->authenticate(gc, [=] {
                repeat_vk_call(gc, req, success_cb, error_cb);
            }, [=] {
                purple_http_request_unref(req);
                if (error_cb)
                    error_cb(picojson::value());
            });
            return;
        } else if (error_code == VK_TOO_MANY_REQUESTS_PER_SECOND) {
            const int RETRY_TIMEOUT = 350; // 350msec is less than 3 requests per second (the current rate limit on Vk.com
            purple_debug_info("prpl-vkcom", "Call rate limit hit, retrying in %d msec\n", RETRY_TIMEOUT);

            timeout_add(gc, RETRY_TIMEOUT, [=] {
                repeat_vk_call(gc, req, success_cb, error_cb);
                return false;
            });
        }
        return;
    } else if (error_code == VK_FLOOD_CONTROL) {
        return; // Simply ignore the error.
    }

    // We do not process captcha requests on API level, but we do not consider them errors
    if (error_code != VK_CAPTCHA_NEEDED)
        purple_debug_error("prpl-vkcom", "Vk.com call error: %s\n", error.serialize().c_str());
    if (error_cb)
        error_cb(error);
}

void repeat_vk_call(PurpleConnection* gc, PurpleHttpRequest* req, const CallSuccessCb& success_cb,
                    const CallErrorCb& error_cb)
{
    http_request(gc, req, [=](PurpleHttpConnection* http_conn, PurpleHttpResponse* response) {
        on_vk_call_cb(http_conn, response, success_cb, error_cb);
    });
    purple_http_request_unref(req);
}

} // End anonymous namespace