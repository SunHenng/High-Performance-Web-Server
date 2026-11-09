-- post_login.lua
-- wrk 登录接口压测脚本
local password = os.getenv("MWS_TEST_PASSWORD") or "change_me"
wrk.method = "POST"
wrk.body   = '{"username":"testuser","password":"' .. password .. '"}'
wrk.headers["Content-Type"] = "application/json"
