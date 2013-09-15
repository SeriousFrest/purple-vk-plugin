#pragma once

#include "common.h"

#include <connection.h>

// Several useful error codes
enum VkErrorCodes {
    // User authorization failed (most likely, access token has expired: redo auth).
    VK_AUTHORIZATION_FAILED = 5,
    // Too many requests per second: send a new request after a moment.
    VK_TOO_MANY_REQUESTS_PER_SECOND = 6,
    // Flood control: message with same guid already sent.
    VK_FLOOD_CONTROL = 9,
    // Captcha needed: user sent too many requests and is required to confirm he is alive
    VK_CAPTCHA_NEEDED = 14
};

// Data, associated with account. It contains all information, required for connecting and executing
// API calls.
class VkConnData
{
public:
    VkConnData(const string& email, const string& password);

    using AuthSuccessCb = std::function<void(void)>;
    void authenticate(PurpleConnection* gc, const AuthSuccessCb& success_cb, const ErrorCb& error_cb = nullptr);

    const string& access_token() const
    {
        return m_access_token;
    }

    const string& uid() const
    {
        return m_uid;
    }

    // If true, connection is in "closing" state. This is set in vk_close and is used in longpoll
    // callback to differentiate the case of network timeout/silent connection dropping and connection
    // cancellation.
    bool is_closing() const
    {
        return m_closing;
    }

    void set_closing()
    {
        m_closing = true;
    }

    // We need to remove all timed events added by timeout_add upon closing connection or the crash
    // is possible otherwise. This set stores all ids of the events.
    set<uint>& timeout_ids()
    {
        return m_timeout_ids;
    }

private:
    string m_email;
    string m_password;
    string m_access_token;
    string m_uid;

    bool m_closing;
    set<uint> m_timeout_ids;
};

// Data, associated with one buddy. See vk.com for documentation on each field.
struct VkBuddyData
{
    string uid;

    string activity;
    string bdate;
    string education;
    string photo_max;
    string mobile_phone;
    string domain;
    bool is_mobile;
};