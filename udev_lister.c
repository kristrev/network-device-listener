#include <stdio.h>
#include <stdlib.h>
#include <libudev.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <libmnl/libmnl.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <string.h>

void set_interface_up_ioctl(const char *devname){
    struct ifreq ifr;
    int32_t fd;

    memcpy(ifr.ifr_name, devname, IFNAMSIZ);
    ifr.ifr_flags = IFF_UP;

    //PF_INET == AF_INET
    if((fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0){
        perror("socket: ");
        return;
    }

    //Get the flags, so that I am sure I dont overwrite anything
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) < 0){
        perror("ioctl (get): ");
        return;
    }

    //I need to "remove" IFF_UP, therefore, use AND
    if(ifr.ifr_flags & IFF_UP)
        ifr.ifr_flags &= ~IFF_UP;
    else
        ifr.ifr_flags |= IFF_UP;

    if(ioctl(fd, SIOCSIFFLAGS, &ifr) < 0){
        perror("ioctl (set): ");
        return;
    }
}

void set_interface_up_netlink(int32_t ifi_idx){
    struct mnl_socket *mnls = NULL;
    uint8_t buf[MNL_SOCKET_BUFFER_SIZE];
    struct nlmsghdr *nl_hdr = NULL;
    struct nlmsgerr *nl_err = NULL;
    struct ifinfomsg *ifi_msg = NULL;
    ssize_t numbytes = 0;

    if((mnls = mnl_socket_open(NETLINK_ROUTE)) == NULL){
        perror("mnl_socket_open: ");
        return;
    }

    nl_hdr = mnl_nlmsg_put_header(buf);
    nl_hdr->nlmsg_flags = NLM_F_REQUEST;
    nl_hdr->nlmsg_type = RTM_NEWLINK;
    
    ifi_msg = mnl_nlmsg_put_extra_header(nl_hdr, sizeof(struct ifinfomsg));
    ifi_msg->ifi_family = AF_UNSPEC;
    ifi_msg->ifi_type = 0;
    ifi_msg->ifi_index = ifi_idx;

    //This is UP, the other is DOWN
    ifi_msg->ifi_flags = IFF_UP;
    //ifi_msg->ifi_flags = ~IFF_UP;
    ifi_msg->ifi_change = 0xFFFFFFFF;
    //mnl_socket_sendto

    if((numbytes = mnl_socket_sendto(mnls, buf, nl_hdr->nlmsg_len)) == -1){
        perror("mnl_socket_sendto: ");
        return;
    }

    fprintf(stderr, "Sent %zd bytes\n", numbytes);

#if 0
    if((numbytes = mnl_socket_recvfrom(mnls, buf, MNL_SOCKET_BUFFER_SIZE)) == -1){
        perror("mnl_socket_recvfrom: ");
        return;
    }

    fprintf(stderr, "Received %zd bytes\n", numbytes);

    nl_hdr = (struct nlmsghdr*) buf;

    if(nl_hdr->nlmsg_type == NLMSG_ERROR){
        nl_err = (struct nlmsgerr*) mnl_nlmsg_get_payload(nl_hdr);
        printf("Errno %d\n", nl_err->error);
    }
#endif

    if(mnl_socket_close(mnls) == -1){
        perror("mnl_socket_close: ");
        return;
    }
}

int main(int argc, char *argv[]){
    struct udev *u_context = NULL;
    struct udev_enumerate *ue_context = NULL;
    struct udev_list_entry *dev_first = NULL;
    struct udev_list_entry *dev = NULL;
    struct udev_device *dev_info = NULL;
    const char *id_usb_driver = NULL;
    const char *interface = NULL;
    const char *ifindex = NULL;
    int32_t if_idx = 0;

    if((u_context = udev_new()) == NULL){
        fprintf(stderr, "Could not create udev context\n");
        exit(EXIT_FAILURE);
    }

    if((ue_context = udev_enumerate_new(u_context)) == NULL){
        fprintf(stderr, "Could not create udev enumerate context\n");
        exit(EXIT_FAILURE);
    }

    if(udev_enumerate_add_match_subsystem(ue_context, "net")){
        fprintf(stderr, "Could not add subsystem match\n");
        exit(EXIT_FAILURE);
    }

    if(udev_enumerate_scan_devices(ue_context)){
        fprintf(stderr, "Could not scan context\n");
        exit(EXIT_FAILURE);
    }

    dev_first = udev_enumerate_get_list_entry(ue_context);

    udev_list_entry_foreach(dev, dev_first){
        //printf("Name %s Value %s\n", udev_list_entry_get_name(dev), udev_list_entry_get_value(dev)); 
        if((dev_info = udev_device_new_from_syspath(u_context, udev_list_entry_get_name(dev))) == NULL)
            continue;
        else{
            id_usb_driver =  udev_device_get_property_value(dev_info, "ID_USB_DRIVER");

            if(id_usb_driver != NULL && !strcmp(id_usb_driver, "cdc_ether")){
                interface = udev_device_get_property_value(dev_info, "INTERFACE");
                ifindex = udev_device_get_property_value(dev_info, "IFINDEX");
                printf("Path %s\n", udev_list_entry_get_name(dev));
                //printf("Driver %s\n", id_usb_driver);
                printf("Interface %s\n", interface);
                if_idx = atoi(ifindex);
                printf("Index %d\n", if_idx);
                set_interface_up_ioctl(interface);
                //set_interface_up_netlink(if_idx);
            }
            udev_device_unref(dev_info);
        }
    }

    //Clean up memory
    udev_enumerate_unref(ue_context);
    udev_unref(u_context);
    exit(EXIT_SUCCESS);
}
