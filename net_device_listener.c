/*
 * Copyright 2013 Kristian Evensen <kristian.evensen@gmail.com>
 *
 * This file is part of Network Device Listener. Network Device Listener is free
 * software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * Network Device Listener is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Network Device Listener. If not, see http://www.gnu.org/licenses/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libudev.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <time.h>
#include <errno.h>

#define LOG_PREFIX "[%d:%d:%d %d/%d/%d]: "
#define DEBUG_PRINT2(fd, ...){fprintf(fd, __VA_ARGS__);fflush(fd);}
//The ## is there so that I dont have to fake an argument when I use the macro
//on string without arguments!
#define DEBUG_PRINT(fd, _fmt, ...){\
    time_t rawtime;\
    struct tm *curtime;\
    time(&rawtime);\
    curtime = gmtime(&rawtime);\
    DEBUG_PRINT2(fd, LOG_PREFIX _fmt, curtime->tm_hour, curtime->tm_min,\
            curtime->tm_sec, curtime->tm_mday, curtime->tm_mon + 1,\
            1900 + curtime->tm_year, ##__VA_ARGS__);}

uint8_t set_interface_up_ioctl(const char *devname){
    struct ifreq ifr;
    int32_t fd;

    memset(&ifr, 0, sizeof(struct ifreq));
    memcpy(ifr.ifr_name, devname, strlen(devname));
    ifr.ifr_flags = IFF_UP;

    //PF_INET == AF_INET
    if((fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0){
        DEBUG_PRINT(stderr, "socket: %s\n", strerror(errno));
        return -1;
    }

    //Get the flags, so that I am sure I dont overwrite anything
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) < 0){
        DEBUG_PRINT(stderr, "ioctl (get): %s\n", strerror(errno));
        return -1;
    }

    //Only set up if needed
    if(!(ifr.ifr_flags & IFF_UP)){
        ifr.ifr_flags |= IFF_UP;
        
        if(ioctl(fd, SIOCSIFFLAGS, &ifr) < 0){
            DEBUG_PRINT(stderr, "ioctl (set): %s\n", strerror(errno));
            return -1;
        }
    } else
        DEBUG_PRINT(stderr, "Interface is already up\n");

    return 0;
}

void configure_device(struct udev_device *device){
    const char *id_usb_driver = NULL;
    const char *interface = NULL;
    const char *ifindex = NULL;
    int32_t if_idx = 0;
        
    interface = udev_device_get_property_value(device, 
            "INTERFACE");
    ifindex = udev_device_get_property_value(device, "IFINDEX");
    if_idx = atoi(ifindex);
    DEBUG_PRINT(stderr, "Found interface %s (idx %d). Will change state if "
            "needed\n", interface, if_idx);
    if(set_interface_up_ioctl(interface))
        DEBUG_PRINT(stderr, "Could not complete set_up on interface\n");
}

//Check for existing cdc_ether interfaces and set them as up
uint8_t check_for_existing_devices(struct udev *u_context){
    struct udev_enumerate *ue_context = NULL;
    struct udev_list_entry *dev_first = NULL;
    struct udev_list_entry *dev = NULL;
    struct udev_device *dev_info = NULL;
    
    //Configure libudev
    if((ue_context = udev_enumerate_new(u_context)) == NULL){
        DEBUG_PRINT(stderr, "Could not create udev enumerate context\n");
        return -1;
    }

    if(udev_enumerate_add_match_subsystem(ue_context, "net")){
        DEBUG_PRINT(stderr, "Could not add subsystem match\n");
        udev_enumerate_unref(ue_context);
        return -1;
    }

    if(udev_enumerate_scan_devices(ue_context)){
        DEBUG_PRINT(stderr, "Could not scan context\n");
        udev_enumerate_unref(ue_context);
        return -1;
    }

    dev_first = udev_enumerate_get_list_entry(ue_context);

    //Loop through all devices matching my filter
    udev_list_entry_foreach(dev, dev_first){
        if((dev_info = udev_device_new_from_syspath(u_context, 
                        udev_list_entry_get_name(dev))) == NULL)
            continue;
        else{
            configure_device(dev_info);
            udev_device_unref(dev_info);
        }
    }

    //Clean up memory
    udev_enumerate_unref(ue_context);
    return 0;
}

uint8_t monitor_devices(struct udev *u_context){
    struct udev_monitor *u_monitor_k = NULL, *u_monitor_u = NULL, 
                        *u_monitor = NULL;
    int32_t efd = 0, udev_fd_k = 0, udev_fd_u = 0, nfds = 0, i = 0;
    struct epoll_event ev, events[2];
    struct udev_device *device = NULL;

    //Acquire context, add filter and bind to start receiving
    //Udev does not behave consistently with some devices (particularily some 
    //modems). I need to listen to kernel to get a reliable notification.
    //TODO: Clean up
    if((u_monitor_u = udev_monitor_new_from_netlink(u_context, "udev")) == 0){
        DEBUG_PRINT(stderr, "Could not get udev monitor (udev)\n");
        return -1;
    }

    if((u_monitor_k = udev_monitor_new_from_netlink(u_context, "kernel")) == 0){
        DEBUG_PRINT(stderr, "Could not get udev monitor\n");
        return -1;
    }

    if(udev_monitor_filter_add_match_subsystem_devtype(u_monitor_u, "net", NULL) 
            < 0){
        DEBUG_PRINT(stderr, "Could not add udev filter (udev)\n");
        udev_monitor_unref(u_monitor_u);
        return -1;
    }

    if(udev_monitor_filter_add_match_subsystem_devtype(u_monitor_k, "net", NULL) 
            < 0){
        DEBUG_PRINT(stderr, "Could not add udev filter (kernel)\n");
        udev_monitor_unref(u_monitor_k);
        return -1;
    }

    if(udev_monitor_enable_receiving(u_monitor_u) < 0){
        DEBUG_PRINT(stderr, "Could not start receiving events (udev)\n");
        udev_monitor_unref(u_monitor_u);
        udev_monitor_unref(u_monitor_k);
        return -1;
    }

    if(udev_monitor_enable_receiving(u_monitor_k) < 0){
        DEBUG_PRINT(stderr, "Could not start receiving events (kernel)\n");
        udev_monitor_unref(u_monitor_u);
        udev_monitor_unref(u_monitor_k);
        return -1;
    }

    //Epoll is so much nicer than select
    if((efd = epoll_create(1)) < 0){
        DEBUG_PRINT(stderr, "Epoll create failed\n");
        udev_monitor_unref(u_monitor_u);
        udev_monitor_unref(u_monitor_k);
        return -1;
    }

    udev_fd_u = udev_monitor_get_fd(u_monitor_u);
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = udev_fd_u;

    if(epoll_ctl(efd, EPOLL_CTL_ADD, udev_fd_u, &ev) < 0){
        DEBUG_PRINT(stderr, "Epoll_ctl failed (udev)\n");
        udev_monitor_unref(u_monitor_k);
        udev_monitor_unref(u_monitor_u);
        return -1;
    }

    udev_fd_k = udev_monitor_get_fd(u_monitor_k);
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = udev_fd_k;

    if(epoll_ctl(efd, EPOLL_CTL_ADD, udev_fd_k, &ev) < 0){
        DEBUG_PRINT(stderr, "Epoll_ctl failed (kernel)\n");
        udev_monitor_unref(u_monitor_k);
        udev_monitor_unref(u_monitor_u);
        return -1;
    }

    while(1){
        if((nfds = epoll_wait(efd, events, 2, -1)) < 0){
            DEBUG_PRINT(stderr, "epoll_wait failed\n");
            udev_monitor_unref(u_monitor_k);
            udev_monitor_unref(u_monitor_u);
            return -1;
        } else {
            for(i = 0; i<nfds; i++){
                if(events[i].data.fd == udev_fd_u)
                    u_monitor = u_monitor_u;
                else
                    u_monitor = u_monitor_k;

                if((device = udev_monitor_receive_device(u_monitor)) != NULL){
                    //Listen for both events due to udevs inconsistent behavior
                    if(!strcmp("add", udev_device_get_action(device)) || 
                            !strcmp("move", udev_device_get_action(device)))
                        configure_device(device);        
                    udev_device_unref(device);
                }
            }
        }
    }

    //Will never be reached, but keep for the sake of completion
    udev_monitor_unref(u_monitor_u);
    udev_monitor_unref(u_monitor_k);
    return 0;
}

void usage(){
    printf("net_device_listener supports the following arguments\n");
    printf("-d : run as daemon. Log messages will be written to /var/log/net_" \
            "device_listener.log\n");
    printf("-h : this output\n");
}

int main(int argc, char *argv[]){
    struct udev *u_context = NULL;
    int32_t c;
    uint8_t run_as_daemon = 0;

    while((c = getopt(argc, argv, "dh")) != -1){
        switch(c){
            case 'd':
                run_as_daemon = 1;
                break;
            case 'h':
                usage();
                exit(EXIT_SUCCESS);
                break;
            default:
                DEBUG_PRINT(stderr, "Unknown parameter\n");
                usage();
                break;
        } 
    }

    if(run_as_daemon){
        daemon(0, 0);
        freopen("/var/log/net_device_listener.log", "a", stderr);
    }

    if((u_context = udev_new()) == NULL){
        DEBUG_PRINT(stderr, "Could not create udev context\n");
        exit(EXIT_FAILURE);
    }

    if(check_for_existing_devices(u_context)){
        DEBUG_PRINT(stderr, "Check for existing devices could not be performed, " 
                "aborting\n");
        exit(EXIT_FAILURE);
    }

    if(monitor_devices(u_context)){
        DEBUG_PRINT(stderr, "Monitoring of devices failed\n");
        exit(EXIT_FAILURE);
    }

    udev_unref(u_context);
    exit(EXIT_SUCCESS);
}
