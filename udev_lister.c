#include <stdio.h>
#include <stdlib.h>
#include <libudev.h>
#include <string.h>

int main(int argc, char *argv[]){
    struct udev *u_context = NULL;
    struct udev_enumerate *ue_context = NULL;
    struct udev_list_entry *dev_first = NULL;
    struct udev_list_entry *dev = NULL;
    struct udev_device *dev_info = NULL;
    const char *id_usb_driver = NULL;
    const char *interface = NULL;

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
                printf("Path %s\n", udev_list_entry_get_name(dev));
                //printf("Driver %s\n", id_usb_driver);
                printf("Interface %s\n", interface);
            }
            udev_device_unref(dev_info);
        }
    }

    //Clean up memory
    udev_enumerate_unref(ue_context);
    udev_unref(u_context);
    exit(EXIT_SUCCESS);
}
