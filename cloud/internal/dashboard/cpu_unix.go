//go:build !windows

package dashboard

import "syscall"

func getCPUUsage() (userUs, sysUs int64, ok bool) {
	var ru syscall.Rusage
	if syscall.Getrusage(syscall.RUSAGE_SELF, &ru) == nil {
		return int64(ru.Utime.Sec)*1e6 + int64(ru.Utime.Usec),
			int64(ru.Stime.Sec)*1e6 + int64(ru.Stime.Usec), true
	}
	return 0, 0, false
}
