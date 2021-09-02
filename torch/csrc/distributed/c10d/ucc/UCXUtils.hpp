#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <ucp/api/ucp.h>
#include <c10/util/Exception.h>
#include <c10/core/DeviceType.h>

namespace c10 {
class UCXError : public c10::Error {
  using Error::Error;
};

} // namespace c10

#define TORCH_UCX_CHECK(st, ...) TORCH_CHECK_WITH(UCXError, (st) == UCS_OK, __VA_ARGS__, " Error: ", ucs_status_string(st))
#define TORCH_UCX_CHECK_PTR(ptr, ...)                                                                \
  do {                                                                                               \
    auto _s_t_a_t_u_s = UCS_PTR_STATUS((ptr));                                                       \
    auto _i_s_o_k = (_s_t_a_t_u_s == UCS_INPROGRESS);                                                \
    TORCH_CHECK_WITH(UCXError, _i_s_o_k, __VA_ARGS__, " Error: ", ucs_status_string(_s_t_a_t_u_s));  \
  } while(0)

namespace c10d {

class UCPWorker;

// When calling UCP async operations like `ucp_tag_send_nbx`, `ucp_tag_recv_nbx`,
// etc., UCP will create a request object in its worker memory pool and return
// the pointer to the user. This request object is used to track the status of
// async operations. It is the user's responsibility to reset the values of these
// objects and free these objects with `ucp_request_free`. Here we use RAII to
// implement this create-by-ucp-and-destroy-by-user logic. Some UCP operations
// finishes immediately. If this is the case, then no request object will be created.
class UCPRequest {
public:
  struct Data {
    ucs_status_t status;
    ucp_tag_recv_info_t info;
    void reset() {
      status = UCS_INPROGRESS;
      info = {};
    }
  };

  static void request_init_callback(void* request);

  ucs_status_t status() const {
    if (data == nullptr) {
      return UCS_OK;
    }
    return data->status;
  }

  const ucp_tag_recv_info_t &info() const {
    TORCH_INTERNAL_ASSERT(data != nullptr);
    return data->info;
  }

  ~UCPRequest();

private:
  // `UCPRequest` objects should only be created by `UCPEndpoint`
  // (for send/recv with an endpoint) or `UCPWorker` (for recv from any source).
  // `UCPRequest` objects are non-copyable: The underlying data should only be
  // allocated by UCP, and it should only be deallocated once.
  friend class UCPWorker;
  friend class UCPEndpoint;
  UCPRequest(const std::shared_ptr<const UCPWorker> &worker, Data *data)
    :data(data), worker(worker) {}
  UCPRequest(const UCPRequest&) = delete;
  UCPRequest& operator=(const UCPRequest &) = delete;

  // Pointer towards the real underlying request object created by UCP.
  // A nullptr represents that a request is finished immediately.
  Data *data;

  std::shared_ptr<const UCPWorker> worker;
};

class UCPEndpoint;

class UCPWorker: public std::enable_shared_from_this<UCPWorker> {
  ucp_worker_h worker;
  static void recv_callback(
    void* request, ucs_status_t status,
    const ucp_tag_recv_info_t* info, void* user_data);
public:
  UCPWorker();
  ucp_worker_h get() const { return worker; }
  ~UCPWorker() { ucp_worker_destroy(worker); }

  // Non-copyable
  UCPWorker(const UCPWorker&) = delete;
  UCPWorker& operator=(const UCPWorker &) = delete;

  using Address = std::vector<uint8_t>;
  Address address() const;
  std::shared_ptr<UCPEndpoint> connect(const Address &address) const;
  unsigned progress() const { return ucp_worker_progress(worker); }

  std::shared_ptr<UCPRequest> submit_p2p_request(c10::DeviceType device, const std::function<ucs_status_ptr_t(const ucp_request_param_t *)> &work) const;
  std::shared_ptr<UCPRequest> recv_with_tag_and_mask(void *data, size_t size, ucp_tag_t tag, ucp_tag_t tag_mask, c10::DeviceType device) const;
};

class UCPEndpoint {
  ucp_ep_h endpoint;
  std::shared_ptr<const UCPWorker> worker;

  // UCPEndpoint should be created by UCPWorker::connect
  UCPEndpoint(const std::shared_ptr<const UCPWorker> &worker, const UCPWorker::Address &address);
  friend UCPWorker;
public:
  ~UCPEndpoint();

  // Non-copyable
  UCPEndpoint(const UCPEndpoint&) = delete;
  UCPEndpoint& operator=(const UCPEndpoint &) = delete;

  // Send data to this endpoint
  std::shared_ptr<UCPRequest> send_with_tag(void *data, size_t size, ucp_tag_t tag, c10::DeviceType device) const;
};

} // namespace c10d