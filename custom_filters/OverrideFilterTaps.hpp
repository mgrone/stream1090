//
// Custom FIR tap overrides for the IQ Filter (-q option)
//
// STREAM1090_OVERRIDE_TAPS allows you to embed your own FIR filter taps directly
// into the binary. When provided, these taps replace the default FIR taps for a
// specific input/output sample‑rate pair.
//
// Supported rate pairs (from the preset table):
//
//   Input → Output
//   -------------------------------
//   Rate_2_4_Mhz  → Rate_8_0_Mhz
//   Rate_6_0_Mhz  → Rate_6_0_Mhz
//   Rate_6_0_Mhz  → Rate_12_0_Mhz
//   Rate_6_0_Mhz  → Rate_24_0_Mhz
//   Rate_10_0_Mhz → Rate_10_0_Mhz
//   Rate_10_0_Mhz → Rate_24_0_Mhz
//
// Usage:
//   STREAM1090_OVERRIDE_TAPS(
//       Rate_<input>_Mhz,
//       Rate_<output>_Mhz,
//       t0, t1, t2, ..., tN
//   )
//
// Notes:
//   • The rate identifiers must match one of the supported preset pairs above.
//   • The number of taps N is flexible; Stream1090 will accept any length.
//   • Symmetric filters are automatically detected and handled efficiently.
//   • Overrides are processed at compile time and completely replace the
//     built‑in tap generator for the selected rate pair.
//   • This feature is intended for advanced users who want to embed custom FIR
//     designs directly into the binary for maximum performance.
//
// Example:
//   STREAM1090_OVERRIDE_TAPS(
//       Rate_6_0_Mhz,
//       Rate_12_0_Mhz,
//       0.1544143,
//       0.262295,
//       0.18557717,
//       0.262295,
//       0.1544143,
//   )


