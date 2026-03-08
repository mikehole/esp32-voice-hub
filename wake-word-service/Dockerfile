FROM python:3.11-slim

WORKDIR /app

# Install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy application
COPY . .

# Expose WebSocket port
EXPOSE 8765

# Environment variables (override at runtime)
ENV PICOVOICE_ACCESS_KEY=""
ENV OPENAI_API_KEY=""
ENV OPENCLAW_URL=""
ENV OPENCLAW_TOKEN=""

CMD ["python", "server.py", "--port", "8765", "--host", "0.0.0.0"]
