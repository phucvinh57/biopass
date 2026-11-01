// Typings for the part of Bun's built-in test runner (`bun test`) the
// *.test.ts files use, so tsc checks them without adding @types/bun.
// Add a matcher here when a test needs one.
declare module "bun:test" {
  interface Matchers {
    not: Matchers;
    toBe(expected: unknown): void;
    toEqual(expected: unknown): void;
    toBeNull(): void;
  }
  export function expect(actual: unknown): Matchers;
  export function test(name: string, fn: () => void | Promise<void>): void;
  export function describe(name: string, fn: () => void): void;
}
