/* Copyright 2015 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#if defined(USE_ACL)

#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/tensor_types.h"
#include "tensorflow/core/framework/variant_op_registry.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/util/bcast.h"
#include "tensorflow/core/kernels/relu_op_functor.h"
#include "tensorflow/core/kernels/softplus_op.h"
#include "tensorflow/core/kernels/cwise_ops.h"
#include "tensorflow/core/kernels/cwise_ops_common.h"
#include "tensorflow/core/common_runtime/device.h"
#include "tensorflow/core/framework/register_types.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/kernels/acl_ops_common.h"

namespace tensorflow {
typedef Eigen::ThreadPoolDevice CPUDevice;
typedef Eigen::GpuDevice GPUDevice;

// Coefficient-wise unary operations:
//   Device: E.g., CPUDevice, GPUDevice.
//   Functor: defined in cwise_ops.h. E.g., functor::sqrt.
template <typename Device, typename Functor, typename T>
class AclUnaryOp : public OpKernel,
  public ACLBaseLayer<arm_compute::CLActivationLayer,arm_compute::NEActivationLayer> {
 public:

  explicit AclUnaryOp(OpKernelConstruction* ctx) : OpKernel(ctx) {
    auto in = DataTypeToEnum<T>::v();
    auto out = DataTypeToEnum<T>::v();
    OP_REQUIRES_OK(ctx, ctx->MatchSignature({in}, {out}));
    if (std::is_same<Functor, functor::sigmoid<T>>::value)
      act_fun_ = arm_compute::ActivationLayerInfo::ActivationFunction::LOGISTIC;
    else if (std::is_same<Functor, functor::tanh<T>>::value)
      act_fun_ = arm_compute::ActivationLayerInfo::ActivationFunction::TANH;
    else if (std::is_same<Functor, functor::Relu<Device, T>>::value)
      act_fun_ = arm_compute::ActivationLayerInfo::ActivationFunction::RELU;
    else if (std::is_same<Functor, functor::Softplus<Device, T>>::value)
      act_fun_ = arm_compute::ActivationLayerInfo::ActivationFunction::SOFT_RELU;
    else
      ctx->CtxFailure(errors::Unimplemented("Acl only sipport sigmoid, tanh," 
            "relu, and softplus"));
  }

  void Compute(OpKernelContext* ctx) override {
    const Tensor& inp = ctx->input(0);
    Tensor* out = nullptr;
    OP_REQUIRES_OK(ctx, ctx->forward_input_or_allocate_output(
                            {0}, 0, inp.shape(), &out));
    RunACLLayer(ctx, inp, out);
  }

private:
  void RunACLLayer(OpKernelContext* ctx, const Tensor& inp, Tensor* out){
    const unsigned int count_in  = inp.shape().num_elements();
    const unsigned int count_out = out->shape().num_elements();
    arm_compute::TensorShape input_shape(count_in);
    arm_compute::TensorShape output_shape(count_out);
    checkreshape(input_shape, is_gpu_);
    if (!this->init_layer_) return;
    this->init_layer_=false;
    if (is_gpu_) new_gpulayer();
    else new_cpulayer();

    this->force_bypass_acl_path_=false;

    arm_compute::ActivationLayerInfo act_info(act_fun_);
    const T* input_data = inp.flat<T>().data();
    T* output_data = out->flat<T>().data();

    if(act_fun_ == arm_compute::ActivationLayerInfo::ActivationFunction::TANH)
      act_info = arm_compute::ActivationLayerInfo(act_fun_, 1.0, 1.0);

    if (is_gpu_) {
        new_tensor(this->gpu().input, input_shape, (void*)input_data);
        new_tensor(this->gpu().output, output_shape, (void*)output_data);
        this->gpu().layer-> configure(this->gpu().input, this->gpu().output,act_info);
    }else{
        new_tensor(this->cpu().input, input_shape, (void*)(input_data));
        new_tensor(this->cpu().output,output_shape,(void*)output_data);
        this->cpu().layer->configure(this->cpu().input, this->cpu().output,act_info);
    }
    acl_run((void*)input_data,(void*)output_data, is_gpu_);
  }

private:
  arm_compute::ActivationLayerInfo::ActivationFunction act_fun_;
};

} // end namespace tensorflow
#endif // define USE_ACL
