from shutil import rmtree
from requests import get
from tempfile import TemporaryDirectory
from sys import platform, argv
from subprocess import call
import os.path as path
def main():
    if platform != "win32":
        print("This script must only be run on Windows!")
        return 1
    response = get("https://vulkan.lunarg.com/sdk/latest/windows.txt", allow_redirects=True)
    vulkan_sdk_version = response.content.decode("utf-8")
    installer_url = f"https://sdk.lunarg.com/sdk/download/{vulkan_sdk_version}/windows/VulkanSDK-{vulkan_sdk_version}-Installer.exe"
    response = get(installer_url, allow_redirects=True)
    tempdir = TemporaryDirectory()
    installer_path = path.join(tempdir.name, "vulkan-installer.exe")
    with open(installer_path, "wb") as stream:
        stream.write(response.content)
        stream.close()
    if call([ installer_path, "/S" ], shell=True) != 0:
        return 1
    rmtree(tempdir.name)
    if len(argv) > 1:
        if argv[1] == "--gh-actions":
            print(f"::set-env name=VULKAN_SDK::C:\\VulkanSDK\\{vulkan_sdk_version}")
    return 0
if __name__ == "__main__":
    exit(main())