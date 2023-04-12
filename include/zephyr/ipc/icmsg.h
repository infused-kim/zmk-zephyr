/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_IPC_ICMSG_H_
#define ZEPHYR_INCLUDE_IPC_ICMSG_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/spsc_pbuf.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Icmsg IPC library API
 * @defgroup ipc_icmsg_api Icmsg IPC library API
 * @ingroup ipc
 * @{
 */

enum icmsg_state {
	ICMSG_STATE_OFF,
	ICMSG_STATE_BUSY,
	ICMSG_STATE_READY,
};

struct icmsg_config_t {
	uintptr_t tx_shm_addr;
	uintptr_t rx_shm_addr;
	size_t tx_shm_size;
	size_t rx_shm_size;
	struct mbox_channel mbox_tx;
	struct mbox_channel mbox_rx;
};

struct icmsg_data_t {
	/* Tx/Rx buffers. */
	struct spsc_pbuf *tx_ib;
	struct spsc_pbuf *rx_ib;
	atomic_t send_buffer_reserved;

	/* Callbacks for an endpoint. */
	const struct ipc_service_cb *cb;
	void *ctx;

	/* General */
	const struct icmsg_config_t *cfg;
	struct k_work_delayable notify_work;
	struct k_work mbox_work;
	atomic_t state;
	uint8_t rx_buffer[CONFIG_IPC_SERVICE_ICMSG_CB_BUF_SIZE] __aligned(4);

	/* No-copy */
#ifdef CONFIG_IPC_SERVICE_ICMSG_NOCOPY_RX
	atomic_t rx_buffer_held;
#endif
};

/** @brief Open an icmsg instance
 *
 *  Open an icmsg instance to be able to send and receive messages to a remote
 *  instance.
 *  This function is blocking until the handshake with the remote instance is
 *  completed.
 *  This function is intended to be called late in the initialization process,
 *  possibly from a thread which can be safely blocked while handshake with the
 *  remote instance is being pefromed.
 *
 *  @param[in] conf Structure containing configuration parameters for the icmsg
 *                  instance.
 *  @param[inout] dev_data Structure containing run-time data used by the icmsg
 *                         instance.
 *  @param[in] cb Structure containing callback functions to be called on
 *                events generated by this icmsg instance. The pointed memory
 *                must be preserved while the icmsg instance is active.
 *  @param[in] ctx Pointer to context passed as an argument to callbacks.
 *
 *
 *  @retval 0 on success.
 *  @retval -EALREADY when the instance is already opened.
 *  @retval other errno codes from dependent modules.
 */
int icmsg_open(const struct icmsg_config_t *conf,
	       struct icmsg_data_t *dev_data,
	       const struct ipc_service_cb *cb, void *ctx);

/** @brief Close an icmsg instance
 *
 *  Closing an icmsg instance results in releasing all resources used by given
 *  instance including the shared memory regions and mbox devices.
 *
 *  @param[in] conf Structure containing configuration parameters for the icmsg
 *                  instance being closed. Its content must be the same as used
 *                  for creating this instance with @ref icmsg_open.
 *  @param[inout] dev_data Structure containing run-time data used by the icmsg
 *                         instance.
 *
 *  @retval 0 on success.
 *  @retval other errno codes from dependent modules.
 */
int icmsg_close(const struct icmsg_config_t *conf,
		struct icmsg_data_t *dev_data);

/** @brief Send a message to the remote icmsg instance.
 *
 *  @param[in] conf Structure containing configuration parameters for the icmsg
 *                  instance.
 *  @param[inout] dev_data Structure containing run-time data used by the icmsg
 *                         instance.
 *  @param[in] msg Pointer to a buffer containing data to send.
 *  @param[in] len Size of data in the @p msg buffer.
 *
 *
 *  @retval 0 on success.
 *  @retval -EBUSY when the instance has not finished handshake with the remote
 *                 instance.
 *  @retval -ENODATA when the requested data to send is empty.
 *  @retval -EBADMSG when the requested data to send is too big.
 *  @retval -ENOBUFS when there are no TX buffers available.
 *  @retval other errno codes from dependent modules.
 */
int icmsg_send(const struct icmsg_config_t *conf,
	       struct icmsg_data_t *dev_data,
	       const void *msg, size_t len);

/** @brief Get an empty TX buffer to be sent using @ref icmsg_send_nocopy
 *
 *  This function can be called to get an empty TX buffer so that the
 *  application can directly put its data into the sending buffer avoiding copy
 *  performed by the icmsg library.
 *
 *  It is the application responsibility to correctly fill the allocated TX
 *  buffer with data and passing correct parameters to @ref
 *  icmsg_send_nocopy function to perform data no-copy-send mechanism.
 *
 *  The size parameter can be used to request a buffer with a certain size:
 *  - if the size can be accommodated the function returns no errors and the
 *    buffer is allocated
 *  - if the requested size is too big, the function returns -ENOMEM and the
 *    the buffer is not allocated.
 *  - if the requested size is '0' the buffer is allocated with the maximum
 *    allowed size.
 *
 *  In all the cases on return the size parameter contains the maximum size for
 *  the returned buffer.
 *
 *  When the function returns no errors, the buffer is intended as allocated
 *  and it is released under one of two conditions: (1) when sending the buffer
 *  using @ref icmsg_send_nocopy (and in this case the buffer is automatically
 *  released by the backend), (2) when using @ref icmsg_drop_tx_buffer on a
 *  buffer not sent.
 *
 *  @param[in] conf Structure containing configuration parameters for the icmsg
 *                  instance.
 *  @param[inout] dev_data Structure containing run-time data used by the icmsg
 *                         instance.
 *  @param[out] data Pointer to the empty TX buffer.
 *  @param[inout] size Pointer to store the requested TX buffer size. If the
 *		       function returns -ENOMEM, this parameter returns the
 *		       maximum allowed size.
 *
 *  @retval -ENOBUFS when there are no TX buffers available.
 *  @retval -EALREADY when a buffer was already claimed and not yet released.
 *  @retval -ENOMEM when the requested size is too big (and the size parameter
 *		    contains the maximum allowed size).
 *
 *  @retval 0 on success.
 */
int icmsg_get_tx_buffer(const struct icmsg_config_t *conf,
			struct icmsg_data_t *dev_data,
			void **data, size_t *size);

/** @brief Drop and release a TX buffer
 *
 *  Drop and release a TX buffer. It is possible to drop only TX buffers
 *  obtained by using @ref icmsg_get_tx_buffer.
 *
 *  @param[in] conf Structure containing configuration parameters for the icmsg
 *                  instance.
 *  @param[inout] dev_data Structure containing run-time data used by the icmsg
 *                         instance.
 *  @param[in] data Pointer to the TX buffer.
 *
 *  @retval -EALREADY when the buffer was already dropped.
 *  @retval -ENXIO when the buffer was not obtained using @ref
 *		   ipc_service_get_tx_buffer
 *
 *  @retval 0 on success.
 */
int icmsg_drop_tx_buffer(const struct icmsg_config_t *conf,
			 struct icmsg_data_t *dev_data,
			 const void *data);

/** @brief Send a message from a buffer obtained by @ref icmsg_get_tx_buffer
 *         to the remote icmsg instance.
 *
 *  This is equivalent to @ref icmsg_send but in this case the TX buffer must
 *  have been obtained by using @ref icmsg_get_tx_buffer.
 *
 *  The API user has to take the responsibility for getting the TX buffer using
 *  @ref icmsg_get_tx_buffer and filling the TX buffer with the data.
 *
 *  After the @ref icmsg_send_nocopy function is issued the TX buffer is no
 *  more owned by the sending task and must not be touched anymore unless the
 *  function fails and returns an error.
 *
 *  If this function returns an error, @ref icmsg_drop_tx_buffer can be used
 *  to drop the TX buffer.
 *
 *  @param[in] conf Structure containing configuration parameters for the icmsg
 *                  instance.
 *  @param[inout] dev_data Structure containing run-time data used by the icmsg
 *                         instance.
 *  @param[in] msg Pointer to a buffer containing data to send.
 *  @param[in] len Size of data in the @p msg buffer.
 *
 *
 *  @return Size of sent data on success.
 *  @retval -EBUSY when the instance has not finished handshake with the remote
 *                 instance.
 *  @retval -ENODATA when the requested data to send is empty.
 *  @retval -EBADMSG when the requested data to send is too big.
 *  @retval -ENXIO when the buffer was not obtained using @ref
 *		   ipc_service_get_tx_buffer
 *  @retval other errno codes from dependent modules.
 */
int icmsg_send_nocopy(const struct icmsg_config_t *conf,
		      struct icmsg_data_t *dev_data,
		      const void *msg, size_t len);

#ifdef CONFIG_IPC_SERVICE_ICMSG_NOCOPY_RX
/** @brief Hold RX buffer to be used outside of the received callback.
 *
 *  @param[in] conf Structure containing configuration parameters for the icmsg
 *                  instance.
 *  @param[inout] dev_data Structure containing run-time data used by the icmsg
 *                         instance.
 *  @param[in] data Pointer to the buffer to be held.
 *
 *  @retval 0 on success.
 *  @retval -EBUSY when the instance has not finished handshake with the remote
 *                 instance.
 *  @retval -EINVAL when the @p data argument does not point to a valid RX
 *                  buffer.
 *  @retval -EALREADY when the buffer is already held.
 */
int icmsg_hold_rx_buffer(const struct icmsg_config_t *conf,
			 struct icmsg_data_t *dev_data,
			 const void *data);

/** @brief Release RX buffer for future use.
 *
 *  @param[in] conf Structure containing configuration parameters for the icmsg
 *                  instance.
 *  @param[inout] dev_data Structure containing run-time data used by the icmsg
 *                         instance.
 *  @param[in] data Pointer to the buffer to be released.
 *
 *  @retval 0 on success.
 *  @retval -EBUSY when the instance has not finished handshake with the remote
 *                 instance.
 *  @retval -EINVAL when the @p data argument does not point to a valid RX
 *                  buffer.
 *  @retval -EALREADY when the buffer is not held.
 */
int icmsg_release_rx_buffer(const struct icmsg_config_t *conf,
			    struct icmsg_data_t *dev_data,
			    const void *data);
#endif

/** @brief Clear memory in TX buffer.
 *
 *  This function is intended to be called at an early stage of boot process,
 *  before the instance is initialized and before the remote core has started.
 *
 *  @param[in] conf Structure containing configuration parameters for the icmsg
 *                  instance being created.
 *
 *  @retval 0 on success.
 */
int icmsg_clear_tx_memory(const struct icmsg_config_t *conf);

/** @brief Clear memory in RX buffer.
 *
 *  This function is intended to be called at an early stage of boot process,
 *  before the instance is initialized and before the remote core has started.
 *
 *  @param[in] conf Structure containing configuration parameters for the icmsg
 *                  instance being created.
 *
 *  @retval 0 on success.
 */
int icmsg_clear_rx_memory(const struct icmsg_config_t *conf);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_IPC_ICMSG_H_ */