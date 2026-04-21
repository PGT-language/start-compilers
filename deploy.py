import subprocess
import sys
import getpass


IMAGE_NAME = "pgt-language"
DOCKER_USER = "pablaofficeal"
VERSION = "latest"

FULL_LATEST = f"{DOCKER_USER}/{IMAGE_NAME}:latest"
FULL_VERSION = f"{DOCKER_USER}/{IMAGE_NAME}:{VERSION}"


def run(cmd):
    print(f"\n🚀 {cmd}")
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"\n❌ Ошибка при выполнении: {cmd}")
        sys.exit(1)


print("📦 Build image...")
run(f"docker build -t {IMAGE_NAME} .")

print("🏷 Tag latest...")
run(f"docker tag {IMAGE_NAME} {FULL_LATEST}")

print("🏷 Tag version...")
run(f"docker tag {IMAGE_NAME} {FULL_VERSION}")

print("🔐 Login Docker Hub...")
run("docker login")

print("📤 Push latest...")
run(f"docker push {FULL_LATEST}")

print("📤 Push version...")
run(f"docker push {FULL_VERSION}")

print("\n✅ ГОТОВО")
print(f"👉 {FULL_LATEST}")
print(f"👉 {FULL_VERSION}")