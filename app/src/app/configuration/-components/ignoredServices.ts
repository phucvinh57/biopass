/**
 * Service names from the comma-separated "Ignored PAM Services" input:
 * trimmed, without empties, first occurrence kept.
 */
export function parseIgnoredServicesInput(raw: string): string[] {
  return raw
    .split(",")
    .map((service) => service.trim())
    .filter((service, index, services) => {
      if (!service) return false;
      return services.indexOf(service) === index;
    });
}

export function areServicesEqual(left: string[], right: string[]): boolean {
  return (
    left.length === right.length &&
    left.every((service, index) => service === right[index])
  );
}
