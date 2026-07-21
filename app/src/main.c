#include <stdlib.h>

#include <ipc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_io.h>

int main(void)
{
	printk("Example actor app\n");

	int rc = ipc_start_all_actors();
	if (rc != 0) {
		printk("actors start failed: %d\n", rc);
		return rc;
	}
	printk("actors started and ready to go\n");

	return 0;
}
