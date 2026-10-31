-- Where a recipe takes its sources: a repository added by path is a working copy and builds as it is;
-- one added by git URL downloads the release archive of the requested version.

-- A new release: its tag here with the sha256 of https://github.com/ElectroZybr/Lattice/archive/refs/tags/v<version>.tar.gz
local versions = {
    ["1.0.0"] = "52a43668d54cb4d1e88db8482db754d90d65a3703ebaeb6b4123a7c304767c98",
}

function main(package)
    local url = package:repo() and package:repo():url() or ""
    if not url:find("://", 1, true) and not url:startswith("git@") then
        package:set("sourcedir", path.join(package:scriptdir(), "../../.."))
        return
    end
    package:add("urls", "https://github.com/ElectroZybr/Lattice/archive/refs/tags/v$(version).tar.gz")
    for version, sha256 in pairs(versions) do
        package:add("versions", version, sha256)
    end
end
