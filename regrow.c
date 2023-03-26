#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include "xdg-shell-client-protocol.h"
#include <math.h>
#include <stdbool.h>

struct client_state{
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct wl_compositor *compositor;
    struct xdg_wm_base *xdg_wm_base;
    struct wl_seat *wl_seat;

    struct wl_surface *wl_surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wl_pointer *wl_pointer;

    uint8_t offset;
    uint32_t last_frame;
    uint16_t height_render;
    uint16_t width_render;
    uint16_t currentRow;
    uint16_t tree_size;
    uint16_t branch_width;
    uint16_t tree_type;
    bool mouse_clicked;
    bool is_drawing;

    struct wl_buffer* treeBuffer;
    struct wl_buffer* emptyBuffer;
    struct wl_surface* wl_cursor_surface;
    struct wl_cursor_image* wl_cursor_image;
};

static void randname(char *buf){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME,&ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r&15) + (r&16)*2;
        r >>= 2;
    }
}

static int create_shm_file(){
    int retries = 100;
    do{
        char name[] = "/wl_shm-XXXXXX";
        randname(name+ sizeof(name)-7);
        retries--;
        int fd = shm_open(name,O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0){
            if(shm_unlink(name) == 0) {
                return fd;
            }
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

int allocate_shm_file(size_t size){
    int fd = create_shm_file();
    if (fd<0){
        return -1;
    }
    int ret;
    do {
        ret = ftruncate(fd,size);
    } while (ret<0 && errno == EINTR);
    if (ret<0){
        close(fd);
        return -1;
    }
    return fd;
}

static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial){
    xdg_wm_base_pong(xdg_wm_base,serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
        .ping = xdg_wm_base_handle_ping
};

void wl_pointer_enter_handle(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y){
    struct client_state *state = data;
    wl_pointer_set_cursor(wl_pointer, serial, state->wl_cursor_surface, state->wl_cursor_image->hotspot_x, state->wl_cursor_image->hotspot_y);
    printf("enter:\t%d %d\n",wl_fixed_to_int(surface_x),wl_fixed_to_int(surface_y));
}

void wl_pointer_leave_handle(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface){
    printf("Pointer left\n");
}

void wl_pointer_motion_handle(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y){
    printf("Move:\t%d %d\n",wl_fixed_to_int(surface_x),wl_fixed_to_int(surface_y));
}

void wl_pointer_button_handle(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state){
    printf("button: 0x%x state: %d\n", button, state);
}

void wl_pointer_axis_handle(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value){
    printf("axis: %d %f\n", axis, wl_fixed_to_double(value));
}

void wl_pointer_frame_handle(void *data, struct wl_pointer *wl_pointer){

}

static const struct wl_pointer_listener wl_pointer_listener = {
        .enter = wl_pointer_enter_handle,
        .leave = wl_pointer_leave_handle,
        .motion = wl_pointer_motion_handle,
        .button = wl_pointer_button_handle,
        .axis = wl_pointer_axis_handle,
        .frame = wl_pointer_frame_handle
};

// gets called when new objects are added
static void registry_handle_global(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface, uint32_t version){
//    printf("Interface: %s, version: %d, name: %d\n",interface,version,name);
      struct client_state *state = data;
      if (strcmp(interface,wl_compositor_interface.name)==0){
          state->compositor = wl_registry_bind(wl_registry,name,&wl_compositor_interface,wl_compositor_interface.version);
      }
      else if(strcmp(interface,wl_shm_interface.name)==0){
          state->shm = wl_registry_bind(wl_registry,name,&wl_shm_interface,wl_shm_interface.version);
      }
      else if(strcmp(interface,xdg_wm_base_interface.name)==0){
          state->xdg_wm_base = wl_registry_bind(wl_registry,name,&xdg_wm_base_interface,4);
          xdg_wm_base_add_listener(state->xdg_wm_base,&xdg_wm_base_listener,state);
      }else if(strcmp(interface, wl_seat_interface.name) == 0){
          state->wl_seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, wl_seat_interface.version);
      }
}


// gets called when objects are removed
static void registry_handle_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name){
    //TODO
}

// poziva se kada compositor vise ne koristi buffer
static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer){
    //wl_buffer_destroy(wl_buffer);
}

static const struct wl_registry_listener registry_listener = {
        .global = registry_handle_global,
        .global_remove = registry_handle_global_remove
};

static const struct wl_buffer_listener buffer_listener = {
        .release = wl_buffer_release
};

void draw_tree_new(uint32_t* data, int position, uint16_t width, uint16_t height, uint16_t tree_size, uint16_t branch_width){
    if (tree_size>20 && branch_width>20 && position-width*tree_size-width*50>0) {
        for (int i = 0; i < tree_size; ++i) {
            data[position] = 0xFFA52A2A;
            position -= width;
        }
        for (int i = 0; i < branch_width; ++i) {
            data[position - i - ((width) * (int) (50 * sin(i * 3.1414 / 180)))] = 0xFF00FF00;
            data[position + i - ((width) * (int) (50 * sin(i * 3.1414 / 180)))] = 0xFF00FF00;
        }
        tree_size-=tree_size/5;
        branch_width/=2;
        draw_tree_new(data,position,width,height,tree_size,branch_width);
    }
}

void draw_branches(uint32_t* data,int position, uint16_t* width, uint16_t* height, uint16_t* branch_size){
    if (position>(*width)*(*branch_size)) {
        for (int i = 0; i <(*branch_size); i++) {
            data[position-((*width)*i)-i] = 0xFF00FF00;
            data[position-((*width)*i)+i] = 0xFF00FF00;
        }
        draw_branches(data,position-((*width)*(*branch_size))-(*branch_size),width,height,branch_size);
        draw_branches(data,position-((*width)*(*branch_size))+(*branch_size),width,height,branch_size);
    }
}

void draw_tree(uint32_t* data, uint16_t width, uint16_t height, uint16_t branch_size){
    int position = width/2+width*(height-height/4);
    branch_size=branch_size/2;
    for (int i = height; i > height-height/4; i--) {
        data[width/2+width*i]=0xFFA52A2A;
    }
    draw_branches(data,position,&width,&height,&branch_size);
}

static struct wl_buffer *draw_frame(struct client_state *state){
    const int width = 1920, height=1000;
    // each pixel contains 4 bytes
    const int stride = width*4;
    // velicina buffera
    const int shm_pool_size = height * stride;
    int fd = allocate_shm_file(shm_pool_size);

    // mmap vraca pointer na alociranu memoriju
    uint32_t *pool_data = mmap(NULL,shm_pool_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    // struktura koja moze drzati buffere
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm,fd,shm_pool_size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,width,height,stride,WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    int position;
    int bar_size=128;
    int offset = state->offset%bar_size;
    // writing the "pixels"(bytes) to the buffer

    if (state->tree_type==0) {
        // draw tree
        draw_tree(pool_data, width, height, state->branch_width);
    }else{
        //draw new tree
        draw_tree_new(pool_data,width/2+width*(height-1),width,height,state->tree_size,state->branch_width);

    }
//    for (int y = 0; y < height; ++y) {
//        for (int x = 0; x < width; ++x) {
//            position = (x+y+offset)%bar_size;
//            if(position<bar_size/4){
//                pool_data[x+y*width]=0xFF000000;
//            }
//            else if(position<bar_size/2){
//                pool_data[x+y*width]=0xFFFF0000;
//            }
//            else if(position<bar_size-bar_size/4){
//                pool_data[x+y*width]=0xFF00FF00;
//            }
//            else{
//                pool_data[x+y*width]=0xFF0000FF;
//            }
//        }
//    }

    munmap(pool_data,shm_pool_size);
    wl_buffer_add_listener(buffer,&buffer_listener, NULL);
    return buffer;
}

static struct wl_buffer* create_empty_buffer(struct client_state* state){
    const int width = 1920, height=1000;
    // each pixel contains 4 bytes
    const int stride = width*4;
    // velicina buffera
    const int shm_pool_size = height * stride;
    // create a file with random name of given pool size filled with "0"
    int fd = allocate_shm_file(shm_pool_size);

    // mmap alocira memoriju i vraca pointer na nju
    uint32_t *pool_data = mmap(NULL,shm_pool_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    // zatrazimo od compositora da sebi mapira istu memoriju kao i client. POOL NIJE BUFFER
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm,fd,shm_pool_size);
    // iz pool-a mozemo alocirati buffere zadane velicine koji pocinju sa zadanim offsetom u poolu i imaju zadani format
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,width,height,stride,WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    munmap(pool_data,shm_pool_size);
    wl_buffer_add_listener(buffer,&buffer_listener, NULL);
    return buffer;
}

void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial){
    struct client_state *state = data;
    //acknowledge that the next frame is ready
    xdg_surface_ack_configure(state->xdg_surface,serial);

    wl_surface_attach(state->wl_surface,state->emptyBuffer,0,0);
    wl_surface_commit(state->wl_surface);
}

static const struct xdg_surface_listener surface_listener = {
        .configure = xdg_surface_handle_configure
};

static const struct wl_callback_listener wl_surface_frame_listener;

void wl_surface_frame_done (void *data, struct wl_callback *wl_callback, uint32_t callback_data){
    struct client_state *state = data;

    // destroy and create a new listener for the next frame
    wl_callback_destroy(wl_callback);
    wl_callback = wl_surface_frame(state->wl_surface);
    wl_callback_add_listener(wl_callback,&wl_surface_frame_listener,state);

    if (state->last_frame!=0){
        state->offset++;
        if (state->height_render < 1000)
            state->height_render++;
        if (state->width_render<1920)
            state->width_render++;
    }
    if (state->is_drawing && state->currentRow>0) {
        state->currentRow -= 5;
        wl_surface_attach(state->wl_surface,state->treeBuffer,0,0);
    }else{
        state->is_drawing=false;
        state->currentRow +=5;
        if (state->currentRow==1000){
            wl_buffer_destroy(state->treeBuffer);
            state->is_drawing=true;
            state->branch_width=rand()%(1920/2)+50;
            state->tree_size=rand()%(1000/2)+100;
            state->tree_type=rand()%2;
            state->treeBuffer = draw_frame(state);
            wl_surface_attach(state->wl_surface,state->treeBuffer,0,0);
        }else{
            wl_surface_attach(state->wl_surface,state->emptyBuffer,0,0);
        }
    }
    //struct wl_buffer *buffer = draw_frame(state);
    wl_surface_damage_buffer(state->wl_surface,0,state->currentRow,1920,1);
    wl_surface_commit(state->wl_surface);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME,&ts);
    state->last_frame = ts.tv_sec;
}

static const struct wl_callback_listener wl_surface_frame_listener = {
        .done = wl_surface_frame_done
};

void map_cursor_image(void* data){
    struct client_state* state = data;
    struct wl_cursor_theme* wl_cursor_theme = wl_cursor_theme_load("breeze_cursors", 24, state->shm);
    struct wl_cursor* wl_cursor = wl_cursor_theme_get_cursor(wl_cursor_theme,"left_ptr");
    state->wl_cursor_image = wl_cursor->images[0];
    struct wl_buffer*  wl_cursor_buffer = wl_cursor_image_get_buffer(state->wl_cursor_image);
    state->wl_cursor_surface = wl_compositor_create_surface(state->compositor);
    wl_surface_attach(state->wl_cursor_surface, wl_cursor_buffer, 0, 0);
    wl_surface_commit(state->wl_cursor_surface);
    state->wl_pointer = wl_seat_get_pointer(state->wl_seat);
}

// TODO: displaying the tree with arrows up/down
// up - loads a buffer with the tree
// down - loads an empty buffer to overwrite/delete the tree
int main(int argc, char *argv[]){
    srand(time(NULL));
    struct client_state state = {0};
    state.currentRow=1000;
    state.height_render=0;
    state.width_render=0;
    state.offset=0;
    state.last_frame=0;
    state.branch_width=rand()%(1920/2)+50;
    state.tree_size=rand()%(1000/2)+100;
    state.tree_type=rand()%2;
    state.mouse_clicked=false;
    state.is_drawing=true;
    // connects the display with the given name(NULL=regrow-0)
    state.display = wl_display_connect(NULL);
    if(!state.display){
        fprintf(stderr,"Error!\n");
        return -1;
    }
    fprintf(stdout,"Connected!\n");

    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry,&registry_listener,&state);
    // waits until pending requests and events are processed
    wl_display_roundtrip(state.display);

    // initialize default buffers
    state.emptyBuffer = create_empty_buffer(&state);
    state.treeBuffer = draw_frame(&state);

    // setup cursor
    map_cursor_image(&state);
    wl_pointer_add_listener(state.wl_pointer, &wl_pointer_listener, &state);

    state.wl_surface = wl_compositor_create_surface(state.compositor);

    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base,state.wl_surface);
    xdg_surface_add_listener(state.xdg_surface, &surface_listener,&state);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_title(state.xdg_toplevel,"My window!");

    wl_surface_commit(state.wl_surface);

    struct wl_callback *wl_callback = wl_surface_frame(state.wl_surface);
    wl_callback_add_listener(wl_callback,&wl_surface_frame_listener,&state);

    // waits until a server sends an event
    while(wl_display_dispatch(state.display) != -1){
        // code that executes after an event is processed
    }
    wl_display_disconnect(state.display);
    return 0;
}
