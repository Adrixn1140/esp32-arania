#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define KEYPAD_ROWS 4
#define KEYPAD_COLS 3

#define KEYPAD_NO_KEY 0

/**
 * @brief Configure rows (input + pull-up) and columns (output, idle HIGH).
 */
void keypad_init(void);

/**
 * @brief Scan the matrix and return a pressed key (edge-triggered, debounced).
 *
 * @param[out] col 0..KEYPAD_COLS-1 (only valid when a key is returned)
 * @param[out] row 0..KEYPAD_ROWS-1 (only valid when a key is returned)
 *
 * @return pressed key char, or KEYPAD_NO_KEY if no (new) key is pressed.
 */
char keypad_get_key(int *col, int *row);

#ifdef __cplusplus
}
#endif