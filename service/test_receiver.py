"""
Test script for PC Receiver

Sends sample voltage data to the local receiver to verify it's working.
"""

import json
import time
import requests
from datetime import datetime

# Configuration
SERVER_URL = "http://localhost:52501"

def send_test_data(voltage, threshold=11.50):
    """Send test voltage data to the receiver."""
    data = {
        "voltage": voltage,
        "threshold": threshold,
        "timestamp": int(time.time()),
        "ssid": "TestNetwork",
        "ip": "192.168.1.100",
        "emailEnabled": True,
        "emailSender": "test@example.com",
        "emailRecipient": "alerts@example.com",
        "hasAppPassword": True,
        "repeatAlertHours": 2.00,
        "history": {
            "points": [
                {"voltage": 12.34, "epoch": int(time.time()) - 300},
                {"voltage": 12.38, "epoch": int(time.time())}
            ],
            "intervalMinutes": 5,
            "totalHours": 48,
            "threshold": threshold
        }
    }
    
    try:
        response = requests.post(SERVER_URL, json=data, timeout=5)
        response.raise_for_status()
        
        print(f"✓ Success! Voltage {voltage}V sent at {datetime.now().strftime('%H:%M:%S')}")
        print(f"  Response: {response.json()}")
        return True
        
    except requests.exceptions.ConnectionError:
        print(f"✗ Error: Could not connect to {SERVER_URL}")
        print("  Is the receiver server running?")
        print("  Run: python pc_receiver.py")
        return False
        
    except requests.exceptions.Timeout:
        print(f"✗ Error: Request timed out")
        return False
        
    except requests.exceptions.HTTPError as e:
        print(f"✗ Error: {e}")
        print(f"  Response: {e.response.text}")
        return False
        
    except Exception as e:
        print(f"✗ Unexpected error: {e}")
        return False


def main():
    """Run various tests."""
    print("="*60)
    print("ESP32 Voltage Receiver - Test Script")
    print("="*60)
    print(f"Testing server at: {SERVER_URL}")
    print()
    
    # Test 1: Single reading
    print("Test 1: Send single reading...")
    if not send_test_data(12.45):
        return
    print()
    
    # Test 2: Low voltage (below threshold)
    print("Test 2: Send low voltage reading...")
    time.sleep(1)
    send_test_data(11.30)
    print()
    
    # Test 3: High voltage
    print("Test 3: Send high voltage reading...")
    time.sleep(1)
    send_test_data(13.80)
    print()
    
    # Test 4: Multiple readings
    print("Test 4: Send 5 readings over 10 seconds...")
    for i in range(5):
        voltage = 12.00 + (i * 0.1)
        send_test_data(voltage)
        if i < 4:
            time.sleep(2)
    print()
    
    print("="*60)
    print("Tests complete!")
    print()
    print("Check these files:")
    print("  - data/latest.json (should have last reading)")
    print("  - data/history.jsonl (should have all readings)")
    print("  - data/receiver.log (should have server logs)")
    print("="*60)


if __name__ == "__main__":
    main()
