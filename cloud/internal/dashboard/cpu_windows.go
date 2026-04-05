//go:build windows

package dashboard

func getCPUUsage() (userUs, sysUs int64, ok bool) {
	return 0, 0, false
}
