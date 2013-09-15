#include "httputils.h"

PurpleHttpKeepalivePool* keepalive_pool = nullptr;

PurpleHttpKeepalivePool* get_global_keepalive_pool()
{
    if (!keepalive_pool)
        keepalive_pool = purple_http_keepalive_pool_new();
    return keepalive_pool;
}

void destroy_global_keepalive_pool()
{
    if (keepalive_pool) {
        purple_http_keepalive_pool_unref(keepalive_pool);
        keepalive_pool = nullptr;
    }
}


PurpleHttpConnection* http_get(PurpleConnection* gc, const char* url, const HttpCallback& callback)
{
    PurpleHttpRequest* request = purple_http_request_new(url);
    PurpleHttpConnection* hc = http_request(gc, request, callback);
    purple_http_request_unref(request);
    return hc;
}

namespace
{

// Callback helper for http_request.
void http_cb(PurpleHttpConnection* http_conn, PurpleHttpResponse* response, void* user_data)
{
    HttpCallback* callback = (HttpCallback*)user_data;
    (*callback)(http_conn, response);
    delete callback;
}

} // End anonymous namespace

PurpleHttpConnection* http_request(PurpleConnection* gc, PurpleHttpRequest* request, const HttpCallback& callback)
{
    purple_http_request_set_keepalive_pool(request, get_global_keepalive_pool());
    HttpCallback* user_data = new HttpCallback(callback);
    PurpleHttpConnection* hc = purple_http_request(gc, request, http_cb, user_data);
    return hc;
}

namespace
{

// A helper callback for purple_http_request_update_on_redirect. TODO: check for infinite loops.
void http_request_redirect_cb(PurpleHttpConnection* http_conn, PurpleHttpResponse* response, HttpCallback callback)
{
    if (purple_http_response_get_code(response) == 302) {
        PurpleConnection* gc = purple_http_conn_get_purple_connection(http_conn);
        PurpleHttpRequest* request = purple_http_conn_get_request(http_conn);
        const char* new_url = purple_http_response_get_header(response, "Location");
        purple_http_request_set_url(request, new_url);
        http_request(gc, request, [=](PurpleHttpConnection* new_conn, PurpleHttpResponse* new_response) {
            http_request_redirect_cb(new_conn, new_response, callback);
        });
    } else {
        callback(http_conn, response);
    }
}

} // End anonymous namespace

PurpleHttpConnection* http_request_update_on_redirect(PurpleConnection* gc, PurpleHttpRequest* request,
                                                      const HttpCallback& callback)
{
    purple_http_request_set_max_redirects(request, 0);

    return http_request(gc, request, [=](PurpleHttpConnection* http_conn, PurpleHttpResponse* response) {
        http_request_redirect_cb(http_conn, response, callback);
    });
}


void http_request_copy_cookie_jar(PurpleHttpRequest* target, PurpleHttpConnection* source_conn)
{
    PurpleHttpRequest* source_request = purple_http_conn_get_request(source_conn);
    purple_http_request_set_cookie_jar(target, purple_http_request_get_cookie_jar(source_request));
}