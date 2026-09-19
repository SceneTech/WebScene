struct outbound_drag_snapshot final {
    uint64_t id{};
    uint64_t document_generation{};
    uint64_t node_generation{};
    uint64_t frame_generation{};
    uint64_t source_node_id{};
    uint32_t operations{};
    std::vector<std::pair<uint32_t, std::string>> items;
    uint64_t drag_image_node_id{};
    float image_width{};
    std::string origin;
};

outbound_drag_snapshot take_outbound_drag(webscene_engine* engine)
{
    const webscene_outbound_drag_request_v1* request = nullptr;
    for (auto attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_outbound_drag_request_v1(engine);
        if (request == nullptr) std::this_thread::sleep_for(
            std::chrono::milliseconds(2));
    }
    require(request != nullptr, "outbound drag request was not published");
    require(request->struct_size == sizeof(*request) && request->version == 1U,
        "outbound drag request ABI metadata changed");
    require(request->item_count <= 32U,
        "outbound drag item cap was exceeded");
    outbound_drag_snapshot result{
        request->request_id, request->document_generation,
        request->node_generation, request->frame_generation,
        request->source_node_id, request->allowed_operations};
    result.drag_image_node_id = request->drag_image_node_id;
    result.image_width = request->drag_image_width;
    result.origin.assign(request->source_origin == nullptr ? "" : request->source_origin,
        request->source_origin_length);
    for (uint32_t index = 0U; index < request->item_count; ++index) {
        const auto& item = request->items[index];
        require(item.struct_size == sizeof(item) && item.version == 1U,
            "outbound drag item ABI metadata changed");
        result.items.emplace_back(item.kind,
            std::string(item.value == nullptr ? "" : item.value,
                item.value_length));
        require(item.grant_reference_length == 0U,
            "DOM drag invented a native file grant");
    }
    webscene_outbound_drag_request_release_v1(request);
    return result;
}

void complete_outbound_drag(webscene_engine* engine,
    const outbound_drag_snapshot& request, uint32_t status,
    uint32_t operation = 0U)
{
    webscene_outbound_drag_completion_v1 completion{};
    completion.struct_size = sizeof(completion);
    completion.version = 1U;
    completion.request_id = request.id;
    completion.document_generation = request.document_generation;
    completion.status = status;
    completion.selected_operation = operation;
    completion.x = 50;
    completion.y = 20;
    require(webscene_engine_complete_outbound_drag_v1(engine, &completion) != 0U,
        "outbound drag completion was rejected");
}

void test_outbound_drag_browser_contract_and_retirement(webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
      document.body.innerHTML = `
        <a id="link" href="/target" style="position:absolute;left:0;top:0;width:90px;height:30px">link</a>
        <div id="text" draggable="true" style="position:absolute;left:0;top:40px;width:90px;height:30px">selected text</div>
        <img id="image" draggable="true" src="data:image/png;base64,iVBORw0KGgo=" alt="preview"
          style="position:absolute;left:0;top:80px;width:20px;height:20px">
        <div id="cancel" draggable="true" style="position:absolute;left:0;top:120px;width:90px;height:30px">cancel</div>`;
      globalThis.__outboundDrag = [];
      link.addEventListener('dragstart', event => {
        event.dataTransfer.setData('text/plain', 'link label');
        event.dataTransfer.effectAllowed = 'copyLink';
        event.dataTransfer.setDragImage(text, 3, 4);
      });
      for (const node of [link,text,image]) node.addEventListener('dragend', event => {
        __outboundDrag.push([node.id,event.type,event.isTrusted,
          event.dataTransfer === null ? 'missing' : 'present']);
      });
      cancel.addEventListener('dragstart', event => event.preventDefault());
    )JS", "outbound-drag-setup.js");

    uint64_t sequence = 61'000U;
    const auto start = [&](double x, double y) {
        pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, x, y, sequence++, true);
        pointer_move(engine, x + 8, y, sequence++, true);
    };
    start(10, 15);
    auto link = take_outbound_drag(engine);
    require(link.document_generation != 0U && link.node_generation != 0U
            && link.origin == "null"
            && link.operations == (WEBSCENE_OUTBOUND_DRAG_OPERATION_COPY_V1
                | WEBSCENE_OUTBOUND_DRAG_OPERATION_LINK_V1)
            && link.items.size() == 2U
            && link.items[0].first == WEBSCENE_OUTBOUND_DRAG_ITEM_URI_V1
            && link.items[0].second.ends_with("/target")
            && link.items[1] == std::pair<uint32_t, std::string>{
                WEBSCENE_OUTBOUND_DRAG_ITEM_TEXT_V1, "link label"}
            && link.drag_image_node_id != link.source_node_id
            && link.image_width > 0,
        "anchor, script DataTransfer, operations, or drag-image metadata diverged");
    complete_outbound_drag(engine, link, WEBSCENE_OUTBOUND_DRAG_COMPLETED_V1,
        WEBSCENE_OUTBOUND_DRAG_OPERATION_LINK_V1);
    static_cast<void>(evaluate(engine, "__outboundDrag.length", "outbound-drag-end.js"));

    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 18, 15, sequence++, false);
    execute_and_wait(engine, R"JS(
      const selection=getSelection(); selection.removeAllRanges();
      const range=document.createRange(); range.selectNodeContents(text.firstChild);
      selection.addRange(range);
      text.removeAttribute('draggable');
    )JS", "outbound-selection-setup.js");
    start(10, 55);
    auto selection = take_outbound_drag(engine);
    require(selection.items.size() == 1U
            && selection.items[0] == std::pair<uint32_t, std::string>{
                WEBSCENE_OUTBOUND_DRAG_ITEM_TEXT_V1, "selected text"},
        "selection drag did not publish bounded plain text");
    complete_outbound_drag(engine, selection, WEBSCENE_OUTBOUND_DRAG_CANCELLED_V1);

    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 18, 55, sequence++, false);
    start(10, 90);
    auto image = take_outbound_drag(engine);
    require(image.items.size() == 1U
            && image.items[0].first == WEBSCENE_OUTBOUND_DRAG_ITEM_IMAGE_V1
            && image.items[0].second.starts_with("data:image/png"),
        "image drag did not publish image URI metadata");
    complete_outbound_drag(engine, image, WEBSCENE_OUTBOUND_DRAG_FAILED_V1);

    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 18, 90, sequence++, false);
    start(10, 135);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(webscene_engine_take_outbound_drag_request_v1(engine) == nullptr,
        "cancelled dragstart published a native request");

    // A completion from a retired document is accepted into the worker queue
    // but cannot dispatch dragend into the replacement realm.
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 18, 135, sequence++, false);
    execute_and_wait(engine, "text.draggable=true", "outbound-retire-setup.js");
    start(10, 55);
    auto retired = take_outbound_drag(engine);
    execute_and_wait(engine, "document.body.replaceChildren()",
        "outbound-node-retirement.js");
    complete_outbound_drag(engine, retired, WEBSCENE_OUTBOUND_DRAG_CANCELLED_V1);

    // Completion input is bounded and validates status/operation combinations.
    webscene_outbound_drag_completion_v1 invalid{
        sizeof(webscene_outbound_drag_completion_v1), 1U, 1U, 1U,
        WEBSCENE_OUTBOUND_DRAG_CANCELLED_V1,
        WEBSCENE_OUTBOUND_DRAG_OPERATION_COPY_V1, 0, 0, 0U, 0U};
    require(webscene_engine_complete_outbound_drag_v1(engine, &invalid) == 0U,
        "cancelled outbound drag accepted a selected operation");

    const auto started = std::chrono::steady_clock::now();
    for (uint32_t index = 0U; index < 10'000U; ++index) {
        const auto* empty = webscene_engine_take_outbound_drag_request_v1(engine);
        require(empty == nullptr, "retired outbound drag remained queued");
    }
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
        "empty outbound drag polling exceeded the performance gate");
}
