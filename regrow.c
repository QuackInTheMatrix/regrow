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
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include <math.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

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
    struct zxdg_decoration_manager_v1 *zxdg_decoration_manager_v1;
    struct zxdg_toplevel_decoration_v1 *zxdg_toplevel_decoration_v1;
    struct wl_pointer *wl_pointer;
    struct wl_keyboard *wl_keyboard;

    uint8_t offset;
    uint8_t step;
    uint16_t height_render;
    uint16_t width_render;
    uint16_t currentRow;
    uint16_t tree_size;
    uint16_t branch_width;
    uint16_t tree_type;
    uint16_t width;
    uint16_t height;
    bool is_drawing;
    bool closed;

    struct wl_buffer* treeBuffer;
    struct wl_buffer* emptyBuffer;
    struct wl_surface* wl_cursor_surface;
    struct wl_cursor_image* wl_cursor_image;
    struct xkb_state* xkb_state;
    struct xkb_context* xkb_context;
    struct xkb_keymap* xkb_keymap;
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

// event that's emitted when the cursor enters the surface
void wl_pointer_enter_handle(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y){
    struct client_state *state = data;
    wl_pointer_set_cursor(wl_pointer, serial, state->wl_cursor_surface, state->wl_cursor_image->hotspot_x, state->wl_cursor_image->hotspot_y);
    printf("enter:\t%d %d\n",wl_fixed_to_int(surface_x),wl_fixed_to_int(surface_y));
}

// event that's emitted when the cursor leaves the surface
void wl_pointer_leave_handle(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface){
    printf("Pointer left\n");
}

// event that's emitted when the cursor moves on the surface
void wl_pointer_motion_handle(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y){
    printf("Move:\t%d %d\n",wl_fixed_to_int(surface_x),wl_fixed_to_int(surface_y));
}

// event that's emitted when the pointer presses a button while on the surface(left click for example)
void wl_pointer_button_handle(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state){
    printf("button: 0x%x state: %d\n", button, state);
    if (button == 0x110 && state == 1){
        struct client_state* client_state = data;
        xdg_toplevel_move(client_state->xdg_toplevel, client_state->wl_seat, serial);
    }
}

// event that's emitted when the pointer scrolls by mouse wheel, touch pad, etc.
void wl_pointer_axis_handle(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value){
    printf("axis: %d %f\n", axis, wl_fixed_to_double(value));
}

// event that indicates that all pointer events have been received for a given frame
void wl_pointer_frame_handle(void *data, struct wl_pointer *wl_pointer){
    // this is where all the past pointer event data should be processed
}

void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source){

}

void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis){

}

void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete){

}

void wl_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120){

}

static const struct wl_pointer_listener wl_pointer_listener = {
        .enter = wl_pointer_enter_handle,
        .leave = wl_pointer_leave_handle,
        .motion = wl_pointer_motion_handle,
        .button = wl_pointer_button_handle,
        .axis = wl_pointer_axis_handle,
        .frame = wl_pointer_frame_handle,
        .axis_source = wl_pointer_axis_source,
        .axis_stop = wl_pointer_axis_stop,
        .axis_discrete = wl_pointer_axis_discrete,
        .axis_value120 = wl_pointer_axis_value120
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
}

static void wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size){
    struct client_state* state = data;
    if (format==WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1){
        char* map_shm = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map_shm == MAP_FAILED){
            printf("Error allocating shared memory.\n");
        }else{
            struct xkb_keymap* xkb_keymap = xkb_keymap_new_from_string(state->xkb_context, map_shm, format, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map_shm,size);
            close(fd);

            struct xkb_state* xkb_state = xkb_state_new(xkb_keymap);
            xkb_keymap_unref(state->xkb_keymap);
            xkb_state_unref(state->xkb_state);
            state->xkb_keymap = xkb_keymap;
            state->xkb_state = xkb_state;
        }
    }else{
        printf("Wrong keymap format.\n");
    }
}

static void wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys){
    struct client_state* state = data;
    printf("Keyboard entered. Keys pressed:\n");
    uint32_t* key;
    wl_array_for_each(key, keys){
        char buf[128];
        uint32_t keycode = *key + 8;
        xkb_keysym_t sym = xkb_state_key_get_one_sym(state->xkb_state, keycode);
        xkb_keysym_get_name(sym,buf,sizeof(buf));
        printf("Sym: %-12s (%d), ",buf,sym);
        xkb_state_key_get_utf8(state->xkb_state, keycode, buf, sizeof(buf));
        printf("UTF-8: %s\n",buf);
    }
}

static void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface){
    printf("Keyboard left the surface.\n");
}

static void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state){
    struct client_state* client_state = data;
    char buf[128];
    uint32_t keycode = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(client_state->xkb_state, keycode);
    xkb_keysym_get_name(sym,buf,sizeof(buf));
    const char* action = state == WL_KEYBOARD_KEY_STATE_PRESSED ? "Pressed" : "Released";
    printf("%s Sym: %-12s (%d), ",action,buf,sym);
    xkb_state_key_get_utf8(client_state->xkb_state, keycode, buf, sizeof(buf));
    printf("UTF-8: %s\n",buf);
}

static void wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group){
    struct client_state* state = data;
    xkb_state_update_mask(state->xkb_state,mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

// event when a key is held down
static void wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay){

}

static const struct wl_keyboard_listener wl_keyboard_listener = {
        .keymap = wl_keyboard_keymap,
        .enter = wl_keyboard_enter,
        .leave = wl_keyboard_leave,
        .key = wl_keyboard_key,
        .modifiers = wl_keyboard_modifiers,
        .repeat_info = wl_keyboard_repeat_info
};


void wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities){
    struct client_state* state = data;
    bool pointer_capability = capabilities & WL_SEAT_CAPABILITY_POINTER;
    bool keyboard_capability = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;

    // pointer setup/release
    if (pointer_capability && state->wl_pointer == NULL){
        map_cursor_image(data);
        state->wl_pointer = wl_seat_get_pointer(state->wl_seat);
        wl_pointer_add_listener(state->wl_pointer, &wl_pointer_listener, state);
    }else if(!pointer_capability && state->wl_pointer != NULL){
        wl_pointer_release(state->wl_pointer);
        state->wl_pointer = NULL;
    }
    // keyboard setup/release
    if (keyboard_capability && state->wl_keyboard == NULL){
        state->wl_keyboard = wl_seat_get_keyboard(state->wl_seat);
        wl_keyboard_add_listener(state->wl_keyboard, &wl_keyboard_listener, state);
    }else if(!keyboard_capability && state->wl_keyboard != NULL){
        wl_keyboard_release(state->wl_keyboard);
        state->wl_keyboard = NULL;
    }
}

void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name){
    printf("Seat name: %s\n",name);
}

static const struct wl_seat_listener wl_seat_listener = {
    .capabilities = wl_seat_capabilities,
    .name = wl_seat_name
};

// gets called when new objects are added
static void registry_handle_global(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface, uint32_t version){
//    printf("Interface: %s, version: %d, name: %d\n",interface,version,name);
      struct client_state *state = data;
      if (strcmp(interface,wl_compositor_interface.name)==0){
          state->compositor = wl_registry_bind(wl_registry,name,&wl_compositor_interface,5);
      }
      else if(strcmp(interface,wl_shm_interface.name)==0){
          state->shm = wl_registry_bind(wl_registry,name,&wl_shm_interface,wl_shm_interface.version);
      }
      else if(strcmp(interface,xdg_wm_base_interface.name)==0){
          state->xdg_wm_base = wl_registry_bind(wl_registry,name,&xdg_wm_base_interface,4);
          xdg_wm_base_add_listener(state->xdg_wm_base,&xdg_wm_base_listener,state);
      }else if(strcmp(interface, wl_seat_interface.name) == 0){
          state->wl_seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, 8);
          wl_seat_add_listener(state->wl_seat, &wl_seat_listener, state);
      }else if(strcmp(interface, zxdg_decoration_manager_v1_interface.name)==0){
          state->zxdg_decoration_manager_v1 = wl_registry_bind(wl_registry, name, &zxdg_decoration_manager_v1_interface, zxdg_decoration_manager_v1_interface.version);
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

static void draw_frame(struct client_state *state){
    // each pixel contains 4 bytes
    const int stride = state->width*4;
    // velicina buffera
    const int shm_pool_size = state->height * stride;
    int fd = allocate_shm_file(shm_pool_size);

    // mmap vraca pointer na alociranu memoriju
    uint32_t *pool_data = mmap(NULL,shm_pool_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    // struktura koja moze drzati buffere
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm,fd,shm_pool_size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,state->width,state->height,stride,WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    int position;
    int bar_size=128;
    int offset = state->offset%bar_size;
    // writing the "pixels"(bytes) to the buffer

    if (state->tree_type==0) {
        // draw tree
        draw_tree(pool_data, state->width, state->height, state->branch_width);
    }else{
        //draw new tree
        draw_tree_new(pool_data,state->width/2+state->width*(state->height-1),state->width,state->height,state->tree_size,state->branch_width);

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
    state->treeBuffer = buffer;
}

static void create_empty_buffer(struct client_state* state){
    // each pixel contains 4 bytes
    const int stride = state->width*4;
    // velicina buffera
    const int shm_pool_size = state->height * stride;
    // create a file with random name of given pool size filled with "0"
    int fd = allocate_shm_file(shm_pool_size);

    // mmap alocira memoriju i vraca pointer na nju
    uint32_t *pool_data = mmap(NULL,shm_pool_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    // zatrazimo od compositora da sebi mapira istu memoriju kao i client. POOL NIJE BUFFER
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm,fd,shm_pool_size);
    // iz pool-a mozemo alocirati buffere zadane velicine koji pocinju sa zadanim offsetom u poolu i imaju zadani format
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,state->width,state->height,stride,WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    munmap(pool_data,shm_pool_size);
    wl_buffer_add_listener(buffer,&buffer_listener, NULL);
    state->emptyBuffer = buffer;
}

void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial){
    struct client_state *state = data;
    printf("Width: %d, Height: %d\n",state->width, state->height);
    //acknowledge that the next frame is ready
    xdg_surface_ack_configure(state->xdg_surface,serial);

    wl_buffer_destroy(state->treeBuffer);
    wl_buffer_destroy(state->emptyBuffer);
    draw_frame(state);
    create_empty_buffer(state);
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

    if (state->is_drawing){
        state->offset++;
        if (state->height_render < state->height)
            state->height_render++;
        if (state->width_render < state->width)
            state->width_render++;
    }
    if (state->is_drawing && state->currentRow-state->step > 0) {
        state->currentRow -= state->step;
        wl_surface_attach(state->wl_surface,state->treeBuffer,0,0);
    }else{
        state->is_drawing=false;
        state->currentRow += state->step;
        if (state->currentRow==state->height){
            wl_buffer_destroy(state->treeBuffer);
            state->is_drawing=true;
            state->branch_width=rand()%(state->width/2)+50;
            state->tree_size=rand()%(state->height/2)+100;
            state->tree_type=rand()%2;
            draw_frame(state);
            wl_surface_attach(state->wl_surface,state->treeBuffer,0,0);
        }else{
            wl_surface_attach(state->wl_surface,state->emptyBuffer,0,0);
        }
    }
    //struct wl_buffer *buffer = draw_frame(state);
    wl_surface_damage_buffer(state->wl_surface,0,state->currentRow,state->width,1);
    wl_surface_commit(state->wl_surface);
}

static const struct wl_callback_listener wl_surface_frame_listener = {
        .done = wl_surface_frame_done
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states){
    struct client_state* state = data;
    if (width == 0 || height == 0){
        return;
    }
    state->width = width;
    state->height = height;
    state->currentRow = height;
    state->is_drawing = true;
}

static void xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height){

}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel){
    struct client_state* state = data;
    state->closed = true;
}

static void xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities){

}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
        .configure = xdg_toplevel_configure,
        .configure_bounds = xdg_toplevel_configure_bounds,
        .close = xdg_toplevel_close,
        .wm_capabilities = xdg_toplevel_wm_capabilities
};



// TODO: displaying the tree with arrows up/down
// up - loads a buffer with the tree
// down - loads an empty buffer to overwrite/delete the tree
int main(int argc, char *argv[]){
    srand(time(NULL));
    struct client_state state = {0};
    state.width=640;
    state.height=480;
    state.height_render=0;
    state.width_render=0;
    state.offset=0;
    state.branch_width=rand()%(state.width/2)+50;
    state.tree_size=rand()%(state.height/2)+100;
    state.tree_type=rand()%2;
    state.is_drawing=true;
    state.currentRow=state.width;
    state.step = 5;
    state.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
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
    create_empty_buffer(&state);
    draw_frame(&state);

    state.wl_surface = wl_compositor_create_surface(state.compositor);

    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base,state.wl_surface);
    xdg_surface_add_listener(state.xdg_surface, &surface_listener,&state);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_add_listener(state.xdg_toplevel, &xdg_toplevel_listener, &state);
    xdg_toplevel_set_title(state.xdg_toplevel,"My window!");

    state.zxdg_toplevel_decoration_v1 = zxdg_decoration_manager_v1_get_toplevel_decoration(state.zxdg_decoration_manager_v1, state.xdg_toplevel);
    zxdg_toplevel_decoration_v1_set_mode(state.zxdg_toplevel_decoration_v1, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

    wl_surface_commit(state.wl_surface);

    struct wl_callback *wl_callback = wl_surface_frame(state.wl_surface);
    wl_callback_add_listener(wl_callback,&wl_surface_frame_listener,&state);

    // waits until a server sends an event
    while(wl_display_dispatch(state.display) != -1){
        // code that executes after an event is processed
        if (state.closed){
            break;
        }
    }
    wl_display_disconnect(state.display);
    return 0;
}
