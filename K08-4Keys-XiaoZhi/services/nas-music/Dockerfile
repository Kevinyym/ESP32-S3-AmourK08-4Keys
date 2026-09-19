FROM python:3.13-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends ffmpeg \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY nas_music.py /app/nas_music.py

EXPOSE 8090
VOLUME ["/cache"]
ENTRYPOINT ["python3", "/app/nas_music.py"]
