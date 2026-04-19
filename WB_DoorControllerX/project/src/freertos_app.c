/* add user code begin Header */
/**
  ******************************************************************************
  * File Name          : freertos_app.c
  * Description        : Code for freertos applications
  */
/* add user code end Header */

/* Includes ------------------------------------------------------------------*/
#include "freertos_app.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "door_ctrl.h"
#include "comm_task.h"
#include "at32f413_wdt.h"
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */

#define MY_TASK01_STACK_WORDS    128u
#define DOOR_CTRL_STACK_WORDS    1024u
#define COMM_TASK_STACK_WORDS    512u

#define MY_TASK01_PRIORITY       (tskIDLE_PRIORITY)
#define DOOR_CTRL_PRIORITY       (tskIDLE_PRIORITY + 2u)
#define COMM_TASK_PRIORITY       (tskIDLE_PRIORITY + 2u)

/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */
TaskHandle_t door_ctrl_task_handle = NULL;
TaskHandle_t comm_task_handle = NULL;

/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/* task handler */
TaskHandle_t my_task01_handle;

/* Idle task control block and stack */
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];
static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

static StaticTask_t idle_task_tcb;
static StaticTask_t timer_task_tcb;

/* External Idle and Timer task static memory allocation functions */
extern void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer, StackType_t ** ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );
extern void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer, StackType_t ** ppxTimerTaskStackBuffer, uint32_t * pulTimerTaskStackSize );

/*
  vApplicationGetIdleTaskMemory gets called when configSUPPORT_STATIC_ALLOCATION
  equals to 1 and is required for static memory allocation support.
*/
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer, StackType_t ** ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &idle_task_tcb;
  *ppxIdleTaskStackBuffer = &idle_task_stack[0];
  *pulIdleTaskStackSize = (uint32_t)configMINIMAL_STACK_SIZE;
}
/*
  vApplicationGetTimerTaskMemory gets called when configSUPPORT_STATIC_ALLOCATION
  equals to 1 and is required for static memory allocation support.
*/
void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer, StackType_t ** ppxTimerTaskStackBuffer, uint32_t * pulTimerTaskStackSize )
{
  *ppxTimerTaskTCBBuffer = &timer_task_tcb;
  *ppxTimerTaskStackBuffer = &timer_task_stack[0];
  *pulTimerTaskStackSize = (uint32_t)configTIMER_TASK_STACK_DEPTH;
}

/* add user code begin 1 */
static void user_task_create(void)
{
  if (door_ctrl_task_handle == NULL)
  {
    wdt_counter_reload();  /* Feed watchdog before creating door_ctrl_task */
    
    xTaskCreate(door_ctrl_task,
                "door_ctrl",
                DOOR_CTRL_STACK_WORDS,
                NULL,
                DOOR_CTRL_PRIORITY,
                &door_ctrl_task_handle);
    
    wdt_counter_reload();  /* Feed watchdog after creating door_ctrl_task */
  }

  if (comm_task_handle == NULL)
  {
    wdt_counter_reload();  /* Feed watchdog before creating comm_task */
    
    xTaskCreate(comm_task_run,
                "comm_task",
                COMM_TASK_STACK_WORDS,
                NULL,
                COMM_TASK_PRIORITY,
                &comm_task_handle);
    
    wdt_counter_reload();  /* Feed watchdog after creating comm_task */
  }
}

/* add user code end 1 */

/**
  * @brief  initializes all task.
  * @param  none
  * @retval none
  */
void freertos_task_create(void)
{
  /* create my_task01 task */
  xTaskCreate(my_task01_func,
              "my_task01",
              128,
              NULL,
              0,
              &my_task01_handle);
}

/**
  * @brief  freertos init and begin run.
  * @param  none
  * @retval none
  */
void wk_freertos_init(void)
{
  /* add user code begin freertos_init 0 */
  /* Configure watchdog with longer timeout for FreeRTOS initialization */
  /* AT32 IWDG uses 12-bit reload value. Set to maximum (4095 = ~3.2 seconds at DIV128) */
  wdt_register_write_enable(TRUE);
  wdt_divider_set(WDT_CLK_DIV_128);        /* Divide by 128 to slow down counter */
  wdt_reload_value_set(4095u);             /* Maximum reload value (~3.2 sec timeout) */
  wdt_register_write_enable(FALSE);
  wdt_counter_reload();

  /* add user code end freertos_init 0 */

  /* Create user tasks BEFORE entering critical section (so interrupts stay enabled during task creation) */
  /* add user code begin freertos_init 0b */
  user_task_create();
  wdt_counter_reload();
  /* add user code end freertos_init 0b */

  /* enter critical */
  taskENTER_CRITICAL();

  freertos_task_create();
  
  /* add user code begin freertos_init 1 */
  /* User tasks already created above */
  wdt_counter_reload();

  /* add user code end freertos_init 1 */

  /* exit critical */
  taskEXIT_CRITICAL();

  /* start scheduler */
  vTaskStartScheduler();
}

/**
  * @brief my_task01 function.
  * @param  none
  * @retval none
  */
void my_task01_func(void *pvParameters)
{
  /* add user code begin my_task01_func 0 */

  /* add user code end my_task01_func 0 */

  /* add user code begin my_task01_func 2 */

  /* add user code end my_task01_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin my_task01_func 1 */

    vTaskDelay(1);

  /* add user code end my_task01_func 1 */
  }
}


/* add user code begin 2 */

/* Idle task hook: feeds watchdog periodically */
void vApplicationIdleHook(void)
{
  wdt_counter_reload();
}

/* add user code end 2 */

