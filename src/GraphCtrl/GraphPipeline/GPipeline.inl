/***************************
@Author: Chunel
@Contact: chunel@foxmail.com
@File: CFLow.inl
@Time: 2021/4/26 9:16 下午
@Desc:
***************************/

#ifndef CGRAPH_GPIPELINE_INL
#define CGRAPH_GPIPELINE_INL


template<typename T>
CSTATUS GPipeline::registerGElement(GElementPtr* elementRef,
                                    const GElementPtrSet& dependElements,
                                    const std::string& name,
                                    int loop) {
    CGRAPH_FUNCTION_BEGIN
    CGRAPH_ASSERT_INIT(false)

    if (manager_->hasElement(*elementRef)) {
        manager_->deleteElement(*elementRef);    // 每次注册，都默认为是新的节点
    }

    /**
     * 如果T类型是 GElement 的子类，则new T类型的对象，并且放到 manager_ 中去
     * 如果创建成功，则添加依赖信息。
     * 如果添加依赖信息失败，则默认创建节点失败，清空节点信息
     * */
    if (std::is_base_of<GNode, T>::value) {
        (*elementRef) = new(std::nothrow) T();
        CGRAPH_ASSERT_NOT_NULL(*elementRef)
    } else if (std::is_same<GCluster, T>::value) {
        CGRAPH_ASSERT_NOT_NULL(elementRef)
    } else if (std::is_same<GRegion, T>::value) {
        CGRAPH_ASSERT_NOT_NULL(elementRef)
    } else {
        return STATUS_ERR;
    }

    (*elementRef)->setName(name);
    (*elementRef)->setLoop(loop);
    status = addDependElements(*elementRef, dependElements);
    CGRAPH_FUNCTION_CHECK_STATUS

    status = manager_->addElement(dynamic_cast<GElementPtr>(*elementRef));
    element_repository_.insert(*elementRef);
    CGRAPH_FUNCTION_END
}


template<typename T>
GElementPtr GPipeline::createGNode(const GNodeInfo& info) {
    GElementPtr node = nullptr;
    if (std::is_base_of<GNode, T>::value) {
        node = new(std::nothrow) T();
        CSTATUS status = addDependElements(node, info.dependence);
        if (STATUS_OK != status) {
            return nullptr;
        }
        node->setName(info.name);
        node->setLoop(info.loop);
        element_repository_.insert(node);
    }

    return node;
}


template<typename T>
GElementPtr GPipeline::createGNodes(const GElementPtrArr& elements,
                                    const GElementPtrSet& dependElements,
                                    const std::string& name,
                                    int loop) {
    // 如果有一个element为null，则创建失败
    if (std::any_of(elements.begin(), elements.end(),
                    [](GElementPtr element) {
                        return (nullptr == element);
                    })) {
        return nullptr;
    }

    // 如果有一个依赖为null的，则创建失败
    if (std::any_of(dependElements.begin(), dependElements.end(),
                    [](GElementPtr element) {
                        return (nullptr == element);
                    })) {
        return nullptr;
    }

    GElementPtr ptr = nullptr;
    if (std::is_same<GCluster, T>::value) {
        ptr = new(std::nothrow) GCluster();
        CGRAPH_ASSERT_NOT_NULL_RETURN_NULL(ptr)
        for (GElementPtr element : elements) {
            ((GCluster *)ptr)->addElement(element);
        }
    } else if (std::is_same<GRegion, T>::value) {
        CGRAPH_ASSERT_NOT_NULL_RETURN_NULL(this->thread_pool_)
        ptr = new(std::nothrow) GRegion();
        CGRAPH_ASSERT_NOT_NULL_RETURN_NULL(ptr)
        ((GRegion *)ptr)->setThreadPool(this->thread_pool_);
        for (GElementPtr element : elements) {
            ((GRegion *)ptr)->manager_->addElement(element);
        }
    }

    CGRAPH_ASSERT_NOT_NULL_RETURN_NULL(ptr)

    CSTATUS status = addDependElements(ptr, dependElements);
    if (STATUS_OK != status) {
        return nullptr;
    }
    ptr->setName(name);
    ptr->setLoop(loop);
    this->element_repository_.insert(ptr);
    return ptr;
}


#endif //CGRAPH_CFLOW_INL