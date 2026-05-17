# Voltage Divider Schematic for ESP32 Voltage Measurement

Below is a simple schematic for safely measuring up to 12V with your ESP32 using a voltage divider:

```
(Vin: 12V or 9V)
   |
  [100kΩ]
   |
   |------> To ESP32 GPIO34 (Analog Input)
   |
  [30kΩ]
   |
  GND
```

- **100kΩ resistor**: Connects from the positive voltage (battery +) to ESP32 analog pin 34.
- **30kΩ resistor**: Connects from analog pin 34 to ground.
- **Vin**: The voltage you want to measure (up to ~14V for safety).
- **ESP32 GND**: Connect to battery ground.

**Note:**
- Do not connect Vin directly to the ESP32 analog pin!
- Use 1% tolerance resistors for best accuracy.
- Adjust resistor values if you want to measure higher voltages.

---

## How it works
The voltage divider reduces the input voltage to a safe level for the ESP32. The code then calculates the real input voltage using the divider ratio.

If you need a graphical schematic, let me know!