# Testing the Static Dashboard

This guide shows how to test the GitHub Pages dashboard locally before deploying.

## Quick Local Test

### Option 1: Using Python (simplest)

Open PowerShell in the project folder and run:

```powershell
python -m http.server 8000
```

Then open your browser to:
```
http://localhost:8000/
```

The dashboard will load and fetch data from `data/latest.json`.

### Option 2: Using Node.js

If you have Node.js installed:

```powershell
npx http-server -p 8000
```

Then open `http://localhost:8000/`

### Option 3: Open File Directly

You can also open `index.html` directly in your browser, but CORS restrictions may prevent it from loading `data/latest.json`. Use one of the HTTP server options above for best results.

## Testing with Live Data

Once you have the PC receiver running (see [HOWTO_PC_GIT_SYNC.md](HOWTO_PC_GIT_SYNC.md#L1)):

1. Start the local HTTP server (Python or Node.js)
2. The PC receiver will update `data/latest.json` when the ESP32 posts data
3. Refresh your browser to see the updated data
4. The dashboard auto-refreshes every 60 seconds

## Verifying the JSON Format

The sample `data/latest.json` includes:
- `voltage`: Current voltage reading (float)
- `threshold`: Alert threshold (float)
- `timestamp`: Unix epoch timestamp (integer)
- `ssid`: WiFi network name (string)
- `ip`: ESP32 local IP address (string)
- `emailEnabled`: Email alerts on/off (boolean)
- `emailSender`: Gmail sender address (string)
- `emailRecipient`: Alert recipient address (string)
- `hasAppPassword`: Whether app password is stored (boolean)
- `repeatAlertHours`: Repeat reminder interval (float)
- `history`: Object containing:
  - `points`: Array of `{voltage, epoch}` objects
  - `intervalMinutes`: Plot interval (integer)
  - `totalHours`: History span (integer)
  - `threshold`: Threshold value (float)

## Browser Developer Tools

To debug issues:

1. Open browser Developer Tools (F12)
2. Check the Console tab for JavaScript errors
3. Check the Network tab to see if `data/latest.json` loads successfully
4. Verify the JSON response format matches the expected structure

## Common Issues

**"Failed to fetch data"**
- Make sure you're using an HTTP server, not opening the file directly
- Check that `data/latest.json` exists and is valid JSON
- Verify the file path is correct relative to `index.html`

**Chart not rendering**
- Check browser console for errors
- Verify the `history.points` array has valid voltage/epoch data
- Make sure canvas elements are present in the DOM

**Relative timestamps showing incorrect values**
- Verify the `timestamp` field is a Unix epoch (seconds since 1970)
- Check system clock on your PC

## Next Steps

Once local testing works:

1. Push to GitHub repository
2. Enable GitHub Pages (Settings → Pages)
3. Access your dashboard at `https://yourusername.github.io/voltage-monitor/`
4. Set up the PC-to-Git automation to keep data updated
