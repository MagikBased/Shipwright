FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PYTHONPATH=/app \
    JP_ASSIST_PLATFORM_DB=/data/platform.sqlite3

WORKDIR /app

RUN groupadd --system --gid 10001 jpassist \
    && useradd --system --uid 10001 --gid jpassist --home-dir /app jpassist \
    && mkdir -p /data /backups \
    && chown jpassist:jpassist /data /backups

COPY requirements.txt ./requirements.txt
RUN pip install --no-cache-dir --requirement requirements.txt

COPY learning_platform ./learning_platform
COPY web ./web

USER jpassist
EXPOSE 8766
VOLUME ["/data", "/backups"]

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8766/readyz', timeout=3)"

CMD ["uvicorn", "learning_platform.main:app", "--host", "0.0.0.0", "--port", "8766", "--workers", "1"]
