FROM python:3.12.15-alpine3.23@sha256:effc76773dc1a0bfcc327c8b2b0c58e1aa37505017d1f036a3c0850c56aacb76

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PYTHONPATH=/app \
    JP_ASSIST_PLATFORM_DB=/data/platform.sqlite3

WORKDIR /app

RUN addgroup --system --gid 10001 jpassist \
    && adduser --system --disabled-password --no-create-home --uid 10001 --ingroup jpassist jpassist \
    && mkdir -p /data /backups \
    && chown jpassist:jpassist /data /backups

COPY requirements.txt ./requirements.txt
RUN pip install --no-cache-dir --requirement requirements.txt \
    && pip uninstall --yes pip

COPY learning_platform ./learning_platform
COPY web ./web

USER jpassist
EXPOSE 8766
VOLUME ["/data", "/backups"]

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8766/readyz', timeout=3)"

CMD ["uvicorn", "learning_platform.main:app", "--host", "0.0.0.0", "--port", "8766", "--workers", "1"]
