-- post_register.lua
-- wrk 注册接口压测脚本（/dev/urandom 保证多线程用户名不重复）
--
-- ⚠ wrk.format(method, path, headers, body) 的第 3 个参数只要传了表，就会**整体替换**
--   wrk.headers，只回填 Host（见 wrk 自带 src/wrk.lua）。而命令行 `-H "Connection: keep-alive"`
--   恰恰是写进 wrk.headers 的：如果这里直接写 wrk.format("POST", nil, {["Content-Type"]=...}, body)，
--   keep-alive 会被静默吃掉，服务器收不到它就走短连接（SubReactor::WantsKeepAlive()），
--   注册场景的数字会和登录场景不在同一口径上。所以必须先拷贝 wrk.headers 再补字段。

counter = 0

request = function()
    counter = counter + 1
    local f = io.open("/dev/urandom", "rb")
    local bytes = f:read(4)
    f:close()
    local r = 0
    for i = 1, #bytes do
        r = r * 256 + string.byte(bytes, i)
    end
    local username = "user_" .. r .. "_" .. counter
    local password = os.getenv("MWS_TEST_PASSWORD") or "change_me"
    local body = '{"username":"' .. username .. '","password":"' .. password .. '"}'

    -- 拷贝而非直接改 wrk.headers：wrk.format 会往传入的表里回填 Host / Content-Length，
    -- 复用同一张表会把这些键永久留在 wrk.headers 上。
    local headers = {}
    for k, v in pairs(wrk.headers) do
        headers[k] = v
    end
    headers["Content-Type"] = "application/json"

    return wrk.format("POST", nil, headers, body)
end
