/** Select value for "off": auto-select camera, or a disabled feature. */
export const DISABLED_OPTION = "__disabled__";

/**
 * The value a config-backed <Select> shows. `selected` is null when the
 * setting is off; a selected value missing from `options` shows as
 * `unavailableOption` so the user sees it is gone instead of a blank.
 */
export function selectValueFor(
  selected: string | null,
  options: readonly string[],
  unavailableOption: string,
): string {
  if (selected === null) return DISABLED_OPTION;
  return options.includes(selected) ? selected : unavailableOption;
}
