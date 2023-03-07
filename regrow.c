#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <sys/random.h>

struct client_state{
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct wl_compositor *compositor;
    struct xdg_wm_base *xdg_wm_base;

    struct wl_surface *wl_surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    uint8_t offset;
    uint32_t last_frame;
    uint16_t height_render;
    uint16_t width_render;
    uint16_t currentRow;
    uint16_t branch_size;

    struct wl_buffer* treeBuffer;
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
      }
}


// gets called when objects are removed
static void registry_handle_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name){
    //TODO
}

// poziva se kada compositor vise ne koristi buffer
static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer){
    wl_buffer_destroy(wl_buffer);
}

static const struct wl_registry_listener registry_listener = {
        .global = registry_handle_global,
        .global_remove = registry_handle_global_remove
};

static const struct wl_buffer_listener buffer_listener = {
        .release = wl_buffer_release
};

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

    // draw tree
    draw_tree(pool_data,width,height,state->branch_size);


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
    int fd = allocate_shm_file(shm_pool_size);

    // mmap vraca pointer na alociranu memoriju
    uint32_t *pool_data = mmap(NULL,shm_pool_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    // struktura koja moze drzati buffere
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm,fd,shm_pool_size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,width,height,stride,WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    munmap(pool_data,shm_pool_size);
    wl_buffer_add_listener(buffer,&buffer_listener, NULL);
    return buffer;
}

static struct wl_buffer* create_tree_buffer(struct client_state* state){
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

    draw_tree(pool_data,width,height,state->branch_size);

    munmap(pool_data,shm_pool_size);
    wl_buffer_add_listener(buffer,&buffer_listener, NULL);
    return buffer;
}

void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial){
    struct client_state *state = data;
    xdg_surface_ack_configure(state->xdg_surface,serial);

    struct wl_buffer *buffer = create_empty_buffer(data);
    state->treeBuffer = create_tree_buffer(data);
    wl_surface_attach(state->wl_surface,buffer,0,0);
    wl_surface_commit(state->wl_surface);
}

static const struct xdg_surface_listener surface_listener = {
        .configure = xdg_surface_handle_configure
};

static const struct wl_callback_listener wl_surface_frame_listener;

void wl_surface_frame_done (void *data, struct wl_callback *wl_callback, uint32_t callback_data){
    wl_callback_destroy(wl_callback);

    struct client_state *state = data;
    wl_callback = wl_surface_frame(state->wl_surface);
    wl_callback_add_listener(wl_callback,&wl_surface_frame_listener,state);

    if (state->last_frame!=0){
        state->offset++;
        if (state->height_render < 1000)
            state->height_render++;
        if (state->width_render<1920)
            state->width_render++;
    }
    if (state->currentRow>0) {
        state->currentRow -= 20;
    }else{
        state->currentRow = 1000;
        getrandom(&state->branch_size,8,0);
        state->branch_size=state->branch_size%100+50;
    }
    struct wl_buffer *buffer = draw_frame(state);
    wl_surface_attach(state->wl_surface,buffer,0,0);
    wl_surface_damage_buffer(state->wl_surface,0,state->currentRow-1,1920,1);
    wl_surface_commit(state->wl_surface);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME,&ts);
    state->last_frame = ts.tv_sec;
}

static const struct wl_callback_listener wl_surface_frame_listener = {
        .done = wl_surface_frame_done
};

// TODO: displaying the tree with arrows up/down
// up - loads a buffer with the tree
// down - loads an empty buffer to overwrite/delete the tree
int main(int argc, char *argv[]){
    struct client_state state = {0};
    state.currentRow=1000;
    state.height_render=0;
    state.width_render=0;
    state.offset=0;
    state.last_frame=0;
    getrandom(&state.branch_size,8,0);
    state.branch_size=state.branch_size%100+50;
    // connects the display with the given name(NULL=wayland-0)
    state.display = wl_display_connect(NULL);
    if(!state.display){
        fprintf(stderr,"Error!\n");
        return -1;
    }
    fprintf(stdout,"Connected!\n");

    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry,&registry_listener,&state);
    // waits until server processes all pending requests
    wl_display_roundtrip(state.display);

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
