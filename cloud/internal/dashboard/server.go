package dashboard

import (
	"context"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"os"
	"strings"

	"github.com/hashicorp/mdns"

	"survaiv/internal/config"
)

// Serve starts the dashboard HTTP server. Blocks until context is cancelled.
func Serve(ctx context.Context, cfg *config.Config, state *State) error {
	router := NewRouter(state, cfg)
	addr := fmt.Sprintf("%s:%d", cfg.ListenAddr, cfg.Port)

	srv := &http.Server{
		Addr:    addr,
		Handler: router,
	}

	go func() {
		<-ctx.Done()
		srv.Shutdown(context.Background())
	}()

	// Advertise via mDNS so the agent is reachable as <name>.local.
	go advertiseMDNS(ctx, cfg, cfg.Port)

	slog.Info("dashboard server starting", "addr", addr)
	if err := srv.ListenAndServe(); err != http.ErrServerClosed {
		return err
	}
	return nil
}

func advertiseMDNS(ctx context.Context, cfg *config.Config, port int) {
	host := sanitizeHostname(cfg.AgentName())
	if host == "" {
		host = "survaiv"
	}

	// Find a local non-loopback IP for the mDNS record.
	ips := localIPs()

	info := []string{"survaiv agent", "tier=giga"}
	service, err := mdns.NewMDNSService(host, "_http._tcp", "", host+".local.", port, ips, info)
	if err != nil {
		slog.Warn("mDNS service setup failed", "err", err)
		return
	}

	server, err := mdns.NewServer(&mdns.Config{Zone: service})
	if err != nil {
		slog.Warn("mDNS server start failed", "err", err)
		return
	}

	slog.Info("mDNS advertising", "hostname", host+".local", "port", port)

	<-ctx.Done()
	server.Shutdown()
}

func sanitizeHostname(name string) string {
	name = strings.ToLower(name)
	name = strings.Map(func(r rune) rune {
		if r >= 'a' && r <= 'z' || r >= '0' && r <= '9' || r == '-' {
			return r
		}
		if r == ' ' || r == '_' {
			return '-'
		}
		return -1
	}, name)
	if len(name) > 63 {
		name = name[:63]
	}
	return name
}

func localIPs() []net.IP {
	var ips []net.IP
	ifaces, err := net.Interfaces()
	if err != nil {
		return ips
	}
	for _, iface := range ifaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, _ := iface.Addrs()
		for _, addr := range addrs {
			if ipnet, ok := addr.(*net.IPNet); ok && ipnet.IP.To4() != nil {
				ips = append(ips, ipnet.IP)
			}
		}
	}
	// Fallback: check hostname.
	if len(ips) == 0 {
		if hostname, err := os.Hostname(); err == nil {
			if resolved, err := net.LookupIP(hostname); err == nil {
				for _, ip := range resolved {
					if ip.To4() != nil && !ip.IsLoopback() {
						ips = append(ips, ip)
					}
				}
			}
		}
	}
	return ips
}
