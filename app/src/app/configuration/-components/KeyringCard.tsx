import { KeyRound, Loader2, Trash2 } from "lucide-react";
import { type KeyboardEvent, useCallback, useEffect, useState } from "react";
import { toast } from "sonner";
import { type KeyringStatus, MAX_KEYRING_PASSWORD_BYTES } from "@/bindings";
import { cmd } from "@/commands";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";

const KEYRING_GUIDE_URL =
  "https://github.com/phucvinh57/biopass/blob/main/docs/Keyring.md";

function byteLength(text: string): number {
  return new TextEncoder().encode(text).length;
}

// Unknown (null/undefined) when the password was saved by an older version.
function lockText(pcr7?: boolean | null): string | null {
  if (pcr7 == null) return null;
  return pcr7
    ? "Locked to Secure Boot settings."
    : "Not locked to Secure Boot settings.";
}

// Shows whether the login password is saved, sealed by the TPM, so a face or
// fingerprint login can unlock the keyring. Saving happens in a dialog and
// goes straight through pkexec, not through the config page's Save button.
export function KeyringCard() {
  const [status, setStatus] = useState<KeyringStatus | null>(null);
  const [setupOpen, setSetupOpen] = useState(false);
  const [removing, setRemoving] = useState(false);

  const refresh = useCallback(async () => {
    try {
      setStatus(await cmd.keyring.status());
    } catch (err) {
      console.error("Failed to read keyring status:", err);
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const noTpm = status !== null && !status.tpm_available;

  async function forget() {
    if (removing) return;
    setRemoving(true);
    try {
      await cmd.keyring.forget();
      toast.success("Saved password removed.");
    } catch (err) {
      toast.error(`Failed to remove the saved password: ${err}`);
    } finally {
      setRemoving(false);
      void refresh();
    }
  }

  const savedAt =
    status?.saved_at_ms != null
      ? new Date(status.saved_at_ms).toLocaleString()
      : null;
  const lock = status?.saved ? lockText(status.pcr7) : null;

  return (
    <div className="rounded-xl border border-border/50 bg-card/50 backdrop-blur-sm p-6 shadow-lg">
      <h2 className="text-xl font-semibold mb-4 flex items-center gap-2">
        <span className="w-8 h-8 rounded-lg bg-linear-to-br from-amber-500 to-orange-500 flex items-center justify-center">
          <KeyRound className="w-4 h-4 text-white" />
        </span>
        Keyring Unlock
      </h2>

      <div className="grid gap-4">
        <p className="text-xs text-muted-foreground">
          Face and fingerprint logins cannpt unlock your keyring (GNOME Keyring,
          KWallet), because it needs your password. Save it here, encrypted by
          this computer's TPM, to unlock the keyring at login. See{" "}
          <a
            href={KEYRING_GUIDE_URL}
            target="_blank"
            rel="noreferrer"
            className="text-primary hover:underline"
          >
            Keyring guide
          </a>
          .
        </p>

        {/* Shown even without a TPM, so a saved copy can still be removed. */}
        <div className="flex items-center justify-between gap-4 p-3 rounded-lg border border-border">
          <div className="grid gap-0.5">
            <Label className="text-sm font-medium">Status</Label>
            <p className="text-xs text-muted-foreground">
              {status === null
                ? "Checking…"
                : status.saved
                  ? `Password saved${savedAt ? ` on ${savedAt}` : ""}.`
                  : "No password saved."}
            </p>
            {lock && <p className="text-xs text-muted-foreground">{lock}</p>}
          </div>
          <div className="flex shrink-0 gap-2">
            {status?.saved && (
              <Button
                type="button"
                variant="outline"
                onClick={forget}
                disabled={removing}
                className="flex items-center gap-2 cursor-pointer"
              >
                <Trash2 className="w-4 h-4" />
                Remove
              </Button>
            )}
            {status !== null && !noTpm && (
              <Button
                type="button"
                onClick={() => setSetupOpen(true)}
                disabled={removing}
                className="cursor-pointer"
              >
                {status.saved ? "Replace" : "Set up"}
              </Button>
            )}
          </div>
        </div>

        {noTpm && (
          <p className="text-sm text-destructive">
            No TPM was found on this computer, so the password can't be saved.
          </p>
        )}
      </div>

      <KeyringSetupDialog
        open={setupOpen}
        onOpenChange={setSetupOpen}
        initialPcr7={status?.pcr7 ?? true}
        onSaved={refresh}
      />
    </div>
  );
}

interface KeyringSetupDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  // The switch's value when the dialog opens: the saved password's lock, or
  // the default (on) when nothing is saved or it is unknown.
  initialPcr7: boolean;
  onSaved: () => void;
}

function KeyringSetupDialog({
  open,
  onOpenChange,
  initialPcr7,
  onSaved,
}: KeyringSetupDialogProps) {
  const [password, setPassword] = useState("");
  const [bindPcr7, setBindPcr7] = useState(initialPcr7);
  const [busy, setBusy] = useState(false);

  const tooLong = byteLength(password) > MAX_KEYRING_PASSWORD_BYTES;

  // Reset the form each time the dialog opens; clear the password on close.
  useEffect(() => {
    setPassword("");
    if (open) setBindPcr7(initialPcr7);
  }, [open, initialPcr7]);

  function changeOpen(next: boolean) {
    if (!busy) onOpenChange(next);
  }

  async function save() {
    if (password === "" || tooLong || busy) return;
    setBusy(true);
    try {
      await cmd.keyring.save(password, bindPcr7);
      toast.success("Password saved. Your keyring will unlock at login.");
      setPassword("");
      onOpenChange(false);
      onSaved();
    } catch (err) {
      // Keep the dialog open with the user's choices so they can retry.
      toast.error(`Failed to save the password: ${err}`);
    } finally {
      setBusy(false);
    }
  }

  // Enter saves; it must not submit the config form around the card.
  function onKeyDown(event: KeyboardEvent<HTMLInputElement>) {
    if (event.key === "Enter") {
      event.preventDefault();
      void save();
    }
  }

  return (
    <Dialog open={open} onOpenChange={changeOpen}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Set up keyring unlock</DialogTitle>
          <DialogDescription>
            Enter your login password. It is checked, then encrypted by this
            computer's TPM. Biopass doesn't keep a copy.
          </DialogDescription>
        </DialogHeader>

        <div className="grid gap-4">
          <div className="grid gap-2">
            <Label htmlFor="keyring-password" className="text-sm font-medium">
              Login password
            </Label>
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
            {tooLong && (
              <p className="text-xs text-destructive">
                Passwords longer than {MAX_KEYRING_PASSWORD_BYTES} bytes can't
                be saved.
              </p>
            )}
          </div>

          <div className="flex items-center justify-between gap-4 p-3 rounded-lg border border-border">
            <div className="grid gap-0.5">
              <Label
                htmlFor="keyring-bind-pcr7"
                className="text-sm font-medium"
              >
                Lock to Secure Boot settings
              </Label>
              <p className="text-xs text-muted-foreground">
                Safer if the computer is stolen. You'll need to save again after
                a firmware or Secure Boot update.
              </p>
            </div>
            <Switch
              id="keyring-bind-pcr7"
              checked={bindPcr7}
              onCheckedChange={setBindPcr7}
              disabled={busy}
            />
          </div>
        </div>

        <DialogFooter>
          <Button
            type="button"
            onClick={save}
            disabled={busy || password === "" || tooLong}
            className="cursor-pointer"
          >
            {busy && <Loader2 className="w-4 h-4 mr-2 animate-spin" />}
            Save password
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
