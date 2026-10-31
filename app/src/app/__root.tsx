import {
  createRootRoute,
  Link,
  Outlet,
  useRouterState,
} from "@tanstack/react-router";
import { TanStackRouterDevtools } from "@tanstack/react-router-devtools";
import { getVersion } from "@tauri-apps/api/app";
import { Cpu, Laptop, Moon, Settings, Sun, User } from "lucide-react";
import { useEffect, useState } from "react";
import { toast } from "sonner";
import logo from "@/assets/logo.svg";
import { cmd } from "@/commands";
import { Button } from "@/components/ui/button";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { useThemeSync } from "@/hooks/useThemeSync";

function App() {
  const [username, setUsername] = useState("");
  const [version, setVersion] = useState("");
  const { chooseTheme } = useThemeSync();
  const pathname = useRouterState({
    select: (state) => state.location.pathname,
  });

  useEffect(() => {
    const loadUsername = async () => {
      try {
        const u = await cmd.system.getCurrentUsername();
        setUsername(u);
      } catch (err) {
        console.error("Failed to get username:", err);
      }
    };

    const loadVersion = async () => {
      try {
        setVersion(await getVersion());
      } catch (err) {
        console.error("Failed to get app version:", err);
      }
    };

    loadUsername();
    loadVersion();
  }, []);

  useEffect(() => {
    cmd.models
      .ensureDefaults()
      .then((downloaded) => {
        if (downloaded.length > 0)
          toast.success("Downloaded default AI models");
      })
      .catch((err) =>
        toast.error(`${err}. Check your network and restart Biopass.`),
      );
  }, []);

  return (
    <div className="min-h-screen bg-background text-foreground">
      <nav className="sticky top-0 z-50 backdrop-blur-lg bg-background/80 border-b border-border">
        <div className="max-w-6xl mx-auto px-4">
          <div className="flex items-center justify-between h-16">
            <div className="flex items-center gap-8">
              <div className="flex items-center gap-3">
                <img src={logo} className="h-8" alt="Biopass logo" />
                <span className="font-bold text-lg hidden sm:inline-block">
                  Biopass
                </span>
                {version && (
                  <span className="text-xs text-muted-foreground">
                    v{version}
                  </span>
                )}
              </div>

              <div className="flex items-center gap-2">
                <Link
                  to="/configuration"
                  className={`flex items-center gap-2 px-3 py-1.5 rounded-md transition-colors cursor-pointer ${
                    pathname === "/configuration"
                      ? "bg-primary/10 text-primary"
                      : "text-muted-foreground hover:bg-muted hover:text-foreground"
                  }`}
                >
                  <Settings className="w-4 h-4" />
                  <span className="text-sm font-medium">Configuration</span>
                </Link>
                <Link
                  to="/models"
                  className={`flex items-center gap-2 px-3 py-1.5 rounded-md transition-colors cursor-pointer ${
                    pathname === "/models"
                      ? "bg-primary/10 text-primary"
                      : "text-muted-foreground hover:bg-muted hover:text-foreground"
                  }`}
                >
                  <Cpu className="w-4 h-4" />
                  <span className="text-sm font-medium">AI Models</span>
                </Link>
              </div>
            </div>

            {username && (
              <div className="flex items-center gap-4">
                <DropdownMenu>
                  <DropdownMenuTrigger asChild>
                    <Button
                      variant="ghost"
                      size="icon"
                      className="cursor-pointer"
                    >
                      <Sun className="h-[1.2rem] w-[1.2rem] rotate-0 scale-100 transition-all dark:-rotate-90 dark:scale-0" />
                      <Moon className="absolute h-[1.2rem] w-[1.2rem] rotate-90 scale-0 transition-all dark:rotate-0 dark:scale-100" />
                      <span className="sr-only">Toggle theme</span>
                    </Button>
                  </DropdownMenuTrigger>
                  <DropdownMenuContent align="end">
                    <DropdownMenuItem
                      onClick={() => chooseTheme("light")}
                      className="cursor-pointer"
                    >
                      <Sun className="mr-2 h-4 w-4" />
                      <span>Light</span>
                    </DropdownMenuItem>
                    <DropdownMenuItem
                      onClick={() => chooseTheme("dark")}
                      className="cursor-pointer"
                    >
                      <Moon className="mr-2 h-4 w-4" />
                      <span>Dark</span>
                    </DropdownMenuItem>
                    <DropdownMenuItem
                      onClick={() => chooseTheme("system")}
                      className="cursor-pointer"
                    >
                      <Laptop className="mr-2 h-4 w-4" />
                      <span>System</span>
                    </DropdownMenuItem>
                  </DropdownMenuContent>
                </DropdownMenu>

                <div className="flex items-center gap-2 px-3 py-1.5 rounded-full bg-muted/50 border border-border/50">
                  <User className="w-4 h-4 text-muted-foreground" />
                  <span className="text-sm font-medium">{username}</span>
                </div>
              </div>
            )}
          </div>
        </div>
      </nav>

      <main className="max-w-6xl mx-auto">
        <Outlet />
      </main>
    </div>
  );
}

function RootLayout() {
  return (
    <>
      <App />
      <TanStackRouterDevtools />
    </>
  );
}

export const Route = createRootRoute({ component: RootLayout });
