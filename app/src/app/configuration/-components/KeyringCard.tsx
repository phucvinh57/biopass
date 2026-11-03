import { KeyRound, Trash2 } from "lucide-react";
import { type KeyboardEvent, useCallback, useEffect, useState } from "react";
import { toast } from "sonner";
import { type KeyringStatus, MAX_KEYRING_PASSWORD_BYTES } from "@/bindings";
import { cmd } from "@/commands";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";

const KEYRING_GUIDE_URL =
  "https://github.com/phucvinh57/biopass/blob/main/docs/Keyring.md";

function byteLength(text: string): number {
  return new TextEncoder().encode(text).length;
}

// Unknown when the password was saved by an older version.
function lockNote(pcr7?: boolean | null): string {
  if (pcr7 == null) return "";
  return pcr7
    ? ", locked to Secure Boot settings"
    : ", not locked to Secure Boot settings";
}

// Saves the login password, sealed by the TPM, so a face or fingerprint
// login can unlock the keyring. Separate from the config form: it saves
// straight away through pkexec, not with the page's Save button.
export function KeyringCard() {
  const [status, setStatus] = useState<KeyringStatus | null>(null);
  const [password, setPassword] = useState("");
  const [bindPcr7, setBindPcr7] = useState(true);
  const [busy, setBusy] = useState(false);

  // `syncSwitch` sets the switch to how the saved password is locked. Only on
  // load and after a successful save: a failed save keeps the user's choice.
  const refresh = useCallback(async (syncSwitch = false) => {
    try {
      const next = await cmd.keyring.status();
      setStatus(next);
      if (syncSwitch && next.pcr7 !== null) setBindPcr7(next.pcr7);
    } catch (err) {
      console.error("Failed to read keyring status:", err);
    }
  }, []);

  useEffect(() => {
    void refresh(true);
  }, [refresh]);

  const tooLong = byteLength(password) > MAX_KEYRING_PASSWORD_BYTES;
  const noTpm = status !== null && !status.tpm_available;

  async function save() {
    if (password === "" || tooLong || busy) return;
    setBusy(true);
    let saved = false;
    try {
      await cmd.keyring.save(password, bindPcr7);
      saved = true;
      toast.success("Password saved. Your keyring will unlock at login.");
      setPassword("");
    } catch (err) {
      toast.error(`Failed to save the password: ${err}`);
    } finally {
      setBusy(false);
      void refresh(saved);
    }
  }

  async function forget() {
    if (busy) return;
    setBusy(true);
    try {
      await cmd.keyring.forget();
      toast.success("Saved password removed.");
    } catch (err) {
      toast.error(`Failed to remove the saved password: ${err}`);
    } finally {
      setBusy(false);
      void refresh();
    }
  }

  // Enter saves this password; it must not submit the config form.
  function onKeyDown(event: KeyboardEvent<HTMLInputElement>) {
    if (event.key === "Enter") {
      event.preventDefault();
      void save();
    }
  }

  const savedAt =
    status?.saved_at_ms != null
      ? new Date(status.saved_at_ms).toLocaleString()
      : null;

  return (
    <div className="rounded-xl border border-border/50 bg-card/50 backdrop-blur-sm p-6 shadow-lg">
      <h2 className="text-xl font-semibold mb-4 flex items-center gap-2">
        <span className="w-8 h-8 rounded-lg bg-linear-to-br from-amber-500 to-orange-500 flex items-center justify-center">
          <KeyRound className="w-4 h-4 text-white" />
        </span>
        Keyring Unlock
      </h2>

      <div className="grid gap-4">
        <p className="text-xs text-muted-foreground max-w-140">
          Face and fingerprint logins can't unlock your keyring (GNOME Keyring,
          KWallet), because it needs your password. Save it here, encrypted by
          this computer's TPM, to unlock the keyring at login.{" "}
          <a
            href={KEYRING_GUIDE_URL}
            target="_blank"
            rel="noreferrer"
            className="text-primary hover:underline"
          >
            Keyring guide
          </a>
        </p>

        {/* Shown even without a TPM, so a saved copy can still be removed. */}
        <div className="flex items-center justify-between p-3 rounded-lg border border-border">
          <div className="grid gap-0.5">
            <Label className="text-sm font-medium">Status</Label>
            <p className="text-xs text-muted-foreground">
              {status === null
                ? "Checking…"
                : status.saved
                  ? `Password saved${savedAt ? ` on ${savedAt}` : ""}${lockNote(status.pcr7)}.`
                  : "No password saved."}
            </p>
          </div>
          {status?.saved && (
            <Button
              type="button"
              variant="outline"
              onClick={forget}
              disabled={busy}
              className="flex items-center gap-2 cursor-pointer"
            >
              <Trash2 className="w-4 h-4" />
              Remove
            </Button>
          )}
        </div>

        {noTpm ? (
          <p className="text-sm text-destructive">
            No TPM was found on this computer, so the password can't be saved.
          </p>
        ) : (
          <>
            <div className="flex items-center justify-between p-3 rounded-lg border border-border">
              <div className="grid gap-0.5">
                <Label
                  htmlFor="keyring-bind-pcr7"
                  className="text-sm font-medium"
                >
                  Lock to Secure Boot settings
                </Label>
                <p className="text-xs text-muted-foreground max-w-100">
                  Safer if the computer is stolen. You'll need to save again
                  after a firmware or Secure Boot update.
                </p>
              </div>
              <Switch
                id="keyring-bind-pcr7"
                checked={bindPcr7}
                onCheckedChange={setBindPcr7}
                disabled={busy}
              />
            </div>

            <div className="grid gap-2">
              <Label htmlFor="keyring-password" className="text-sm font-medium">
                Login password
              </Label>
              <div className="flex gap-2">
                <Input
                  id="keyring-password"
                  type="password"
                  autoComplete="off"
                  value={password}
                  onChange={(e) => setPassword(e.target.value)}
                  onKeyDown={onKeyDown}
                  disabled={busy}
                  aria-invalid={tooLong}
                />
                <Button
                  type="button"
                  onClick={save}
                  disabled={busy || password === "" || tooLong}
                  className="cursor-pointer"
                >
                  {busy
                    ? "Working…"
                    : status?.saved
                      ? "Replace"
                      : "Save password"}
                </Button>
              </div>
              {tooLong && (
                <p className="text-xs text-destructive">
                  Passwords longer than {MAX_KEYRING_PASSWORD_BYTES} bytes can't
                  be saved.
                </p>
              )}
            </div>
          </>
        )}
      </div>
    </div>
  );
}
