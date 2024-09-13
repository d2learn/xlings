import("common")
import("core.base.json")

function api()
    return "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"
end

function generate_request_data(model, system_data, user_data)
    local data_template = [[{
    "model": "%s",
    "messages": [
        {
            "role": "system",
            "content": "%s"
        },
        {
            "role": "user", 
            "content": "%s"
        }
    ]
}]]
    local escaped_system_data = common.xlings_str_format(system_data)
    local escaped_user_data = common.xlings_str_format(user_data)
    local data = string.format(data_template, model, escaped_system_data, escaped_user_data)
    return data
end

function parse_response(content)
    local data = json.decode(content)
--[[ response example
{
  "choices": [
    {
      "message": {
        "role": "assistant",
        "content": "我是通义千问，由阿里云开发的AI助手。我被设计用来回答各种问题、提供信息和与用户进行对话。有什么我可以帮助你的吗？"
      },
      "finish_reason": "stop",
      "index": 0,
      "logprobs": null
    }
  ],
  "object": "chat.completion",
  "usage": {
    "prompt_tokens": 22,
    "completion_tokens": 36,
    "total_tokens": 58
  },
  "created": 1721044596,
  "system_fingerprint": null,
  "model": "qwen-turbo",
  "id": "chatcmpl-94149c5a-137f-9b87-b2c8-61235e85f540"
}
]]

    

    if data then
        return data["choices"][1]["message"]["content"]
    end
end