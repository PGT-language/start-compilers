FROM python:3.12-slim AS builder

RUN apt-get update && apt-get install -y g++ && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN python3 build.py

FROM debian:stable-slim

WORKDIR /usr/local/bin

COPY --from=builder /app/compile/pgt .

RUN chmod +x /usr/local/bin/pgt

ENTRYPOINT ["pgt"]