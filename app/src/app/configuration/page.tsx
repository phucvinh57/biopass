import { zodResolver } from "@hookform/resolvers/zod";
import { createFileRoute } from "@tanstack/react-router";
import { RotateCcw, Save } from "lucide-react";
import { useTheme } from "next-themes";
import { useEffect, useState } from "react";
import { FormProvider, useForm } from "react-hook-form";
import { toast } from "sonner";
import { cmd } from "@/commands";
import { Button } from "@/components/ui/button";
import { withCurrentAppearance } from "@/hooks/useThemeSync";
import type { BiopassConfig } from "@/types/config";
import { MethodConfig } from "./-components/MethodConfig";
import { StrategyConfig } from "./-components/StrategyConfig";
import { biopassConfigSchema, validateConfig } from "./-components/validation";

function ConfigurationRouteComponent() {
  const [initialConfig, setInitialConfig] = useState<BiopassConfig | null>(
    null,
  );
  const [loadError, setLoadError] = useState<string | null>(null);

  useEffect(() => {
    let canceled = false;

    cmd.config
      .load()
      .then((loadedConfig) => {
        if (!canceled) setInitialConfig(loadedConfig);
      })
      .catch((err) => {
        if (!canceled) setLoadError(String(err));
      });

    return () => {
      canceled = true;
    };
  }, []);

  // No form for a config that can't be read: saving it would replace the
  // user's file with whatever the form holds.
  if (loadError) {
    return (
      <div className="w-full max-w-4xl mx-auto p-6">
        <div className="rounded-md border border-destructive/50 bg-destructive/10 p-4 text-sm">
          <p className="font-medium text-destructive">
            Failed to load config.yaml
          </p>
          <p className="mt-1 text-muted-foreground">{loadError}</p>
          <p className="mt-1 text-muted-foreground">
            Fix or remove the file, then reopen this page.
          </p>
        </div>
      </div>
    );
  }

  if (!initialConfig) {
    return (
      <div className="flex items-center justify-center p-8">
        <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary" />
      </div>
    );
  }

  return <ConfigurationForm initialConfig={initialConfig} />;
}

function ConfigurationForm({
  initialConfig,
}: {
  initialConfig: BiopassConfig;
}) {
  const form = useForm<BiopassConfig>({
    defaultValues: structuredClone(initialConfig),
    resolver: zodResolver(biopassConfigSchema),
  });
  const { isSubmitting, isDirty } = form.formState;
  const { theme } = useTheme();

  async function onSave(values: BiopassConfig) {
    const configToSave = withCurrentAppearance(values, theme);

    try {
      // Inside the try: validateConfig calls commands that can reject.
      const isValid = await validateConfig(configToSave);
      if (!isValid) return;

      await cmd.config.save(configToSave);
      form.reset(configToSave);
      toast.success("Settings saved successfully!");
    } catch (err) {
      console.error("Failed to save config:", err);
      toast.error(`Failed to save config: ${err}`);
    }
  }

  const submit = form.handleSubmit(onSave, () => {
    toast.error("Please fix validation errors before saving");
  });

  useEffect(() => {
    function handleSaveShortcut(event: KeyboardEvent) {
      if (
        !(event.ctrlKey || event.metaKey) ||
        event.key.toLowerCase() !== "s"
      ) {
        return;
      }

      event.preventDefault();
      void submit();
    }

    window.addEventListener("keydown", handleSaveShortcut);

    return () => {
      window.removeEventListener("keydown", handleSaveShortcut);
    };
  }, [submit]);

  function handleReset() {
    form.reset();
    toast.info("Configuration reset to last saved state");
  }

  return (
    <FormProvider {...form}>
      <form
        onSubmit={submit}
        className="flex flex-col gap-6 w-full max-w-4xl mx-auto p-6"
      >
        <div className="flex justify-between items-center">
          <div>
            <h1 className="text-3xl font-bold bg-linear-to-r from-primary to-purple-500 bg-clip-text text-transparent">
              Biopass Configuration
            </h1>
            <p className="text-sm text-muted-foreground mt-1">
              Manage your authentication methods and execution strategies
            </p>
          </div>
          <div className="flex gap-2">
            <Button
              type="button"
              variant="outline"
              onClick={handleReset}
              disabled={!isDirty || isSubmitting}
              className="flex items-center gap-2 cursor-pointer"
            >
              <RotateCcw className="w-4 h-4" />
              Reset
            </Button>
            <Button
              type="submit"
              disabled={isSubmitting}
              className="flex items-center gap-2 cursor-pointer"
            >
              <Save className="w-4 h-4" />
              {isSubmitting ? "Saving..." : "Save"}
            </Button>
          </div>
        </div>

        <div className="grid gap-6">
          <StrategyConfig />
          <MethodConfig />
        </div>
      </form>
    </FormProvider>
  );
}

export const Route = createFileRoute("/configuration/")({
  component: ConfigurationRouteComponent,
});
