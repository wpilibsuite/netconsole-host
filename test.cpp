#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <net/if.h>

int main ()
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_in *sa;
	char *addr;

	getifaddrs (&ifap);
	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET) {
			sa = (struct sockaddr_in *) ifa->ifa_addr;
			addr = inet_ntoa(sa->sin_addr);
			bool inFlag = (ifa->ifa_flags & IFF_BROADCAST) == IFF_BROADCAST;
			char* broad = inet_ntoa(((struct sockaddr_in *)ifa->ifa_broadaddr)->sin_addr);
			printf("Interface: %s\tAddress: %s, BROADCAST: %d -> %s\n", ifa->ifa_name, addr, inFlag, broad);
		}
	}

	freeifaddrs(ifap);
	return 0;
}
