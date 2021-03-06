#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _C_PLUGIN

/*******************************************************************************
 *
 * C plugin
 *
 */

#include <dlfcn.h>
#include <string.h>
#include <alloca.h>

#include "core.h"
#include "utils.h"
#include "main.h"
#include "plugin.h"


/**
 *
 */
int proxenet_c_initialize_vm(plugin_t* plugin)
{
	void *interpreter;

	interpreter = dlopen(plugin->fullpath, RTLD_NOW);
	if (!interpreter) {
		xlog(LOG_ERROR, "Failed to dlopen('%s'): %s\n", plugin->fullpath, dlerror());
		return -1;
	}

	plugin->interpreter = interpreter;
        plugin->interpreter->ready = true;

	return 0;
}


/**
 *
 */
int proxenet_c_destroy_vm(plugin_t* plugin)
{
        if(count_plugins_by_type(_C_) > 0) {
                xlog(LOG_ERROR, "%s\n", "Some C plugins are still running");
                return -1;
        }

        if (!plugin->interpreter->ready){
                xlog(LOG_ERROR, "%s\n", "Cannot destroy un-init dl link");
                return -1;
        }

        if (dlclose((void*)plugin->interpreter) < 0) {
                xlog(LOG_ERROR, "Failed to dlclose() for '%s': %s\n", plugin->name, dlerror());
                return -1;
        }

        plugin->pre_function  = NULL;
        plugin->post_function = NULL;

        plugin->interpreter->ready = false;
        plugin->interpreter = NULL;
        return 0;
}


/**
 *
 */
int proxenet_c_initialize_function(plugin_t* plugin, req_t type)
{
	void *interpreter;

	/* if already initialized, return ok */
	if (plugin->pre_function && type==REQUEST)
		return 0;

	if (plugin->post_function && type==RESPONSE)
		return 0;

	interpreter = (void *) plugin->interpreter;

	if (type == REQUEST) {
		plugin->pre_function = dlsym(interpreter, CFG_REQUEST_PLUGIN_FUNCTION);
		if (plugin->pre_function) {
#ifdef DEBUG
			xlog(LOG_DEBUG, "[C] '%s' request_hook function is at %p\n",
                             plugin->name,
                             plugin->pre_function);
#endif
			return 0;
		}

	} else {
		plugin->post_function = dlsym(interpreter, CFG_RESPONSE_PLUGIN_FUNCTION);
		if (plugin->post_function) {
#ifdef DEBUG
			xlog(LOG_DEBUG, "[C] '%s' response_hook function is at %p\n",
                             plugin->name,
                             plugin->post_function);
#endif
			return 0;
		}

	}

        xlog(LOG_ERROR, "[C] dlsym(%s) failed for '%s': %s\n",
             (type==REQUEST)?"REQUEST":"RESPONSE",
             plugin->name,
             dlerror());

	return -1;
}


/**
 * Execute a proxenet plugin written in C.
 *
 * @note Because there is no other consistent way in C of keeping track of the
 * right size of the request (strlen() will break at the first NULL byte), the
 * signature of functions in a C plugin must include a pointer to the size which
 * **must** be changed by the called plugin.
 * Therefore the definition is
 * char* proxenet_request_hook(unsigned int rid, char* buf, char* uri, size_t* buflen);
 *
 * See examples/ for examples.
 */
char* proxenet_c_plugin(plugin_t *plugin, request_t *request)
{
	char* (*plugin_function)(unsigned long, char*, char*, size_t*);
	char *bufres, *uri;
        size_t buflen;

	bufres = uri = NULL;

        uri = request->uri;
	if (!uri)
		return NULL;

	if (request->type == REQUEST)
		plugin_function = plugin->pre_function;
	else
		plugin_function = plugin->post_function;

        buflen = request->size;
	bufres = (*plugin_function)(request->id, request->data, uri, &buflen);
	if(!bufres){
                request->size = -1;
                goto end_exec_c_plugin;
        }

        request->data = proxenet_xstrdup(request->data, buflen);
        request->size = buflen;

end_exec_c_plugin:
	return bufres;
}

#endif
