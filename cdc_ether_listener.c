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

uint8_t set_interface_up_ioctl(const char *devname){
    struct ifreq ifr;
    int32_t fd;

    memset(&ifr, 0, sizeof(struct ifreq));
    memcpy(ifr.ifr_name, devname, strlen(devname));
    ifr.ifr_flags = IFF_UP;

    //PF_INET == AF_INET
    if((fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0){
        perror("socket: ");
        return -1;
    }

    //Get the flags, so that I am sure I dont overwrite anything
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) < 0){
        perror("ioctl (get): ");
        return -1;
    }

    //Only set up if needed
    if(!ifr.ifr_flags & IFF_UP){
        ifr.ifr_flags |= IFF_UP;
        
        if(ioctl(fd, SIOCSIFFLAGS, &ifr) < 0){
            perror("ioctl (set): ");
            return -1;
        }
    } else
        fprintf(stderr, "Interface is already up\n");

    return 0;
}

void check_device(struct udev_device *device){
    const char *id_usb_driver = NULL;
    const char *interface = NULL;
    const char *ifindex = NULL;
    int32_t if_idx = 0;

    id_usb_driver =  udev_device_get_property_value(device, 
            "ID_USB_DRIVER");

    if(id_usb_driver != NULL && (!strcmp(id_usb_driver, "cdc_ether"))){
        interface = udev_device_get_property_value(device, 
            "INTERFACE");
        ifindex = udev_device_get_property_value(device, "IFINDEX");
        if_idx = atoi(ifindex);
        printf("Found interface %s (idx %d). Will change state if "
                "needed\n", interface, if_idx);
        if(set_interface_up_ioctl(interface))
            fprintf(stderr, "Could not complete set_up on interface\n");
    }
}

//Check for existing cdc_ether interfaces and set them as up
uint8_t check_for_existing_devices(struct udev *u_context){
    struct udev_enumerate *ue_context = NULL;
    struct udev_list_entry *dev_first = NULL;
    struct udev_list_entry *dev = NULL;
    struct udev_device *dev_info = NULL;
    
    //Configure libudev
    if((ue_context = udev_enumerate_new(u_context)) == NULL){
        fprintf(stderr, "Could not create udev enumerate context\n");
        return -1;
    }

    if(udev_enumerate_add_match_subsystem(ue_context, "net")){
        fprintf(stderr, "Could not add subsystem match\n");
        udev_enumerate_unref(ue_context);
        return -1;
    }

    if(udev_enumerate_scan_devices(ue_context)){
        fprintf(stderr, "Could not scan context\n");
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
            check_device(dev_info);
            udev_device_unref(dev_info);
        }
    }

    //Clean up memory
    udev_enumerate_unref(ue_context);
    return 0;
}

uint8_t monitor_devices(struct udev *u_context){
    struct udev_monitor *u_monitor = NULL;
    int32_t efd = 0, udev_fd = 0, nfds = 0;
    struct epoll_event ev;
    struct udev_device *device = NULL;

    //Acquire context, add filter and bind to start receiving
    if((u_monitor = udev_monitor_new_from_netlink(u_context, "udev")) == 0){
        fprintf(stderr, "Could not get udev monitor\n");
        return -1;
    }

    if(udev_monitor_filter_add_match_subsystem_devtype(u_monitor, "net", NULL) 
            < 0){
        fprintf(stderr, "Could not add udev filter\n");
        udev_monitor_unref(u_monitor);
        return -1;
    }

    if(udev_monitor_enable_receiving(u_monitor) < 0){
        fprintf(stderr, "Could not start receiving events\n");
        udev_monitor_unref(u_monitor);
        return -1;
    }

    //Epoll is so much nicer than select
    if((efd = epoll_create(1)) < 0){
        fprintf(stderr, "Epoll create failed\n");
        udev_monitor_unref(u_monitor);
        return -1;
    }

    udev_fd = udev_monitor_get_fd(u_monitor);
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = udev_fd;

    if(epoll_ctl(efd, EPOLL_CTL_ADD, udev_fd, &ev) < 0){
        fprintf(stderr, "Epoll_ctl failed\n");
        udev_monitor_unref(u_monitor);
        return -1;
    }

    while(1){
        if((nfds = epoll_wait(efd, &ev, 1, -1)) < 0){
            fprintf(stderr, "epoll_wait failed\n");
            udev_monitor_unref(u_monitor);
            return -1;
        } else {
            if((device = udev_monitor_receive_device(u_monitor)) == NULL)
                fprintf(stderr, "Could not get information about device\n");
            else {
                printf("Action %s\n", udev_device_get_action(device));
                printf("Devtype %s\n", udev_device_get_devtype(device));
                //fprintf(stderr, "Got some information\n");
            }
        }
    }

    //Will never be reached, but keep for the sake of completion
    udev_monitor_unref(u_monitor);
    return 0;
}

int main(int argc, char *argv[]){
    struct udev *u_context = NULL;
    if((u_context = udev_new()) == NULL){
        fprintf(stderr, "Could not create udev context\n");
        exit(EXIT_FAILURE);
    }

    if(check_for_existing_devices(u_context)){
        fprintf(stderr, "Check for existing devices could not be performed, " 
                "aborting\n");
        exit(EXIT_FAILURE);
    }

    if(monitor_devices(u_context)){
        fprintf(stderr, "Monitoring of devices failed\n");
        exit(EXIT_FAILURE);
    }

    udev_unref(u_context);
    exit(EXIT_SUCCESS);
}
