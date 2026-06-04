#!/usr/bin/env python3
"""
ESP32 Voltage Monitor - PC Receiver Server

Receives voltage data from ESP32 via HTTP POST and saves to local files.
Designed to run continuously and survive restarts.
"""

import json
import sys
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path
import logging
from logging.handlers import TimedRotatingFileHandler

# Configuration
PORT = 52501
DATA_DIR = Path(__file__).parent / "data"
LATEST_FILE = DATA_DIR / "latest.json"
HISTORY_FILE = DATA_DIR / "history.jsonl"
LOG_FILE = DATA_DIR / "receiver.log"
MAX_HISTORY_ENTRIES = 1000  # Limit history.jsonl to last 1000 entries

# Setup logging
DATA_DIR.mkdir(exist_ok=True)
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        TimedRotatingFileHandler(LOG_FILE, when='midnight', interval=1, backupCount=1, encoding='utf-8'),
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger(__name__)


def validate_data(data):
    """Validate the expected JSON structure from ESP32."""
    required_fields = ['voltage', 'threshold', 'timestamp']
    
    if not isinstance(data, dict):
        return False, "Data must be a JSON object"
    
    for field in required_fields:
        if field not in data:
            return False, f"Missing required field: {field}"
    
    # Validate types
    if not isinstance(data.get('voltage'), (int, float)):
        return False, "voltage must be a number"
    
    if not isinstance(data.get('threshold'), (int, float)):
        return False, "threshold must be a number"
    
    if not isinstance(data.get('timestamp'), (int, float)):
        return False, "timestamp must be a number"
    
    return True, "OK"


def rotate_history_file():
    """Keep only the last MAX_HISTORY_ENTRIES in history.jsonl to prevent infinite growth."""
    if not HISTORY_FILE.exists():
        return
    
    try:
        # Read all existing entries
        with open(HISTORY_FILE, 'r') as f:
            lines = f.readlines()
        
        # If we're under the limit, no rotation needed
        if len(lines) <= MAX_HISTORY_ENTRIES:
            return
        
        # Keep only the last MAX_HISTORY_ENTRIES
        kept_lines = lines[-MAX_HISTORY_ENTRIES:]
        
        # Write back the trimmed history
        with open(HISTORY_FILE, 'w') as f:
            f.writelines(kept_lines)
        
        logger.info(f"Rotated history file: kept last {len(kept_lines)} entries, removed {len(lines) - len(kept_lines)}")
    
    except Exception as e:
        logger.error(f"Error rotating history file: {e}")



class VoltageReceiver(BaseHTTPRequestHandler):
    """HTTP request handler for voltage data from ESP32."""
    
    def log_message(self, format, *args):
        """Override to use our logger instead of stderr."""
        logger.info(f"{self.address_string()} - {format % args}")
    
    def do_POST(self):
        """Handle POST requests with voltage data."""
        try:
            # Read the posted data
            content_length = int(self.headers.get('Content-Length', 0))
            if content_length == 0:
                self.send_error(400, "No data received")
                return
            
            body = self.rfile.read(content_length)
            
            # Parse JSON
            try:
                data = json.loads(body.decode('utf-8'))
            except json.JSONDecodeError as e:
                logger.warning(f"Invalid JSON received: {e}")
                self.send_error(400, f"Invalid JSON: {str(e)}")
                return
            
            # Validate data structure
            valid, message = validate_data(data)
            if not valid:
                logger.warning(f"Validation failed: {message}")
                self.send_error(400, f"Validation error: {message}")
                return
            
            # Add server reception timestamp
            data['receivedAt'] = datetime.now().isoformat()
            
            # Write to latest.json (overwrite)
            with open(LATEST_FILE, 'w') as f:
                json.dump(data, f, indent=2)
            logger.info(f"Updated {LATEST_FILE} - Voltage: {data['voltage']}V")
            
            # Append to history.jsonl
            with open(HISTORY_FILE, 'a') as f:
                f.write(json.dumps(data) + '\n')
            
            # Rotate history file to prevent infinite growth
            rotate_history_file()
            
            # Send success response
            response = {
                "status": "success",
                "message": "Data received and saved",
                "timestamp": datetime.now().isoformat()
            }
            
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(response).encode('utf-8'))
            
        except Exception as e:
            logger.error(f"Error processing request: {e}", exc_info=True)
            self.send_error(500, f"Internal server error: {str(e)}")
    
    def do_GET(self):
        """Handle GET requests - return status page."""
        if self.path == '/':
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.end_headers()
            
            status_html = f"""
            <html>
            <head><title>ESP32 Voltage Receiver</title></head>
            <body>
                <h1>ESP32 Voltage Monitor - PC Receiver</h1>
                <p><strong>Status:</strong> Running ✓</p>
                <p><strong>Port:</strong> {PORT}</p>
                <p><strong>Data directory:</strong> {DATA_DIR}</p>
                <p><strong>Latest file:</strong> {LATEST_FILE}</p>
                <p><strong>History file:</strong> {HISTORY_FILE}</p>
                <p><strong>Time:</strong> {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
                <hr>
                <p>POST voltage data to this server to update the files.</p>
            </body>
            </html>
            """
            self.wfile.write(status_html.encode('utf-8'))
        else:
            self.send_error(404, "Not found")


def main():
    """Start the HTTP server."""
    server_address = ('', PORT)
    httpd = HTTPServer(server_address, VoltageReceiver)

    logger.info("="*60)
    logger.info("ESP32 Voltage Monitor - PC Receiver Starting")
    logger.info("="*60)
    logger.info(f"Server listening on http://0.0.0.0:{PORT}")
    logger.info("ESP32 must be configured with the PC's manual IP address")
    logger.info(f"Data directory: {DATA_DIR}")
    logger.info(f"Latest file: {LATEST_FILE}")
    logger.info(f"History file: {HISTORY_FILE}")
    logger.info(f"Log file: {LOG_FILE}")
    logger.info("Press Ctrl+C to stop")
    logger.info("="*60)
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        logger.info("\nShutting down server...")
        httpd.shutdown()
        logger.info("Server stopped")


if __name__ == "__main__":
    main()
