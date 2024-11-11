import("core.base.json")
import("lib.detect.find_tool")

function get_latest_release_url(owner, repo, match_str)

    assert(type(owner) == "string" and owner ~= "", "Invalid owner")
    assert(type(repo) == "string" and repo ~= "", "Invalid repo")
    
    -- API URL
    local api_url = format("https://api.github.com/repos/%s/%s/releases/latest", owner, repo)
    
    -- req
    local content = try
    {
        function()
            local tool = find_tool("curl")
            return os.iorun(tool.program .. " -sL " .. api_url)
        end,
        catch
        {
            function(e)
                cprint("\n\t${bright yellow}Note: Please check your network - Github${clear}\n")
                return nil
            end
        }
    }

    if not content then
        return nil, "Failed to fetch release info"
    end
    
    -- 解析JSON
    local release = json.decode(content)
    if not release or not release.assets then
        return nil, "Invalid release info"
    end
    
    -- match release version
    for _, asset in ipairs(release.assets) do
        if not match_str or asset.name:find(match_str) then
            return {
                version = release.tag_name,
                name = asset.name,
                url = asset.browser_download_url
            }
        end
    end

    return nil, "No matching asset found"
end

function main()
    --local tool = find_tool("project-graph")
    --print(tool)
    --local info = get_latest_release_url("LiRenTech", "project-graph", "exe")
    --print(info)
end