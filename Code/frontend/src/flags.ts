/**
 * @brief Build-time switches for features that ship disabled.
 *
 * Automated heat-loss calibration was hidden for 2.0 because it was not trusted
 * yet. It is back on for the 2.1.0 test build: a whole run passes in the
 * automated tests, the real tub still has to confirm it. The firmware endpoints
 * (/starthlcal/, /cancelhlcal/, /gethlcal/) and the frontend code behind this
 * flag are intact. Manually setting the heat-loss coefficient stays available.
 */
export const SHOW_HEAT_LOSS_CALIBRATION = true; // back on for the 2.1.0 test build
