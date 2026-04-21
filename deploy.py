import subprocess
import sys

IMAGE_NAME = "pgt-runtime"
DOCKER_USER = "yourname"
TAG = "latest"

FULL_IMAGE = f"{DOCKER_USER}/{IMAGE_NAME}:{TAG}"


def run(cmd):
    print(f"[RUN] {cmd}")
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"[ERROR] Command failed: {cmd}")
        sys.exit(1)


print("🚀 Building Docker image...")
run(f"docker build -t {IMAGE_NAME} .")

print("🏷 Tagging image...")
run(f"docker tag {IMAGE_NAME} {FULL_IMAGE}")

print("🔐 Logging into Docker Hub...")
run("docker login")

print("📦 Pushing image...")
run(f"docker push {FULL_IMAGE}")

print(f"✅ Done! Image available at: {FULL_IMAGE}")