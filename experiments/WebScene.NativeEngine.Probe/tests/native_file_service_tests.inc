void test_native_file_service(webscene_engine* engine) {
    execute(engine,"void 0","file-bootstrap.js");
    require(webscene_engine_enable_file_service_v1(engine,1)!=0,"file service opt-in failed");
    execute(engine,R"JS(
      document.body.innerHTML='<input id="native-file" type="file" accept=".kcad,.json" multiple>';
      globalThis.fileEvents=[];
      const input=document.getElementById('native-file');
      input.oncancel=()=>fileEvents.push('cancel');
      input.onchange=async()=>{
        fileEvents.push('change');
        globalThis.openedFiles=await Promise.all(Array.from(input.files, async f=>({name:f.name,text:await f.text(),bytes:Array.from(new Uint8Array(await f.arrayBuffer())),isFile:f instanceof File})));
      };
      input.accept='.dxf';
      input.click();
    )JS","native-file-open.js");
    const auto take=[&] {
        const webscene_file_request_v1* request=nullptr;
        for(int i=0;i<200 && !request;++i) { request=webscene_engine_take_file_request_v1(engine); if(!request) std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        require(request!=nullptr,"native file request missing"); return request;
    };
    auto* request=take();
    require(request->kind==1 && request->multiple && std::string(request->accept)==".dxf","Open metadata mismatch");
    const auto id=request->request_id;
    webscene_file_request_release_v1(request);
    const uint8_t bytes[]={0,1,255,65};
    webscene_file_data_v1 files[]={{"café.kcad","application/json",bytes,sizeof(bytes)},{"empty.json","application/json",nullptr,0}};
    require(webscene_engine_complete_file_request_v1(engine,id,0,files,2,nullptr),"Open completion rejected");
    std::string opened;
    for(int i=0;i<100;++i) {opened=evaluate(engine,"JSON.stringify(globalThis.openedFiles)","opened-files.js");if(opened.find("café.kcad")!=std::string::npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    require(opened.find("[0,1,255,65]")!=std::string::npos && opened.find("empty.json")!=std::string::npos && evaluate(engine,"openedFiles.every(f=>f.isFile)","is-file.js")=="true","Open bytes/File semantics mismatch: "+opened);
    execute(engine,"document.getElementById('native-file').click()","cancel-open.js");
    request=take(); const auto cancel_id=request->request_id;webscene_file_request_release_v1(request);
    require(webscene_engine_complete_file_request_v1(engine,cancel_id,1,nullptr,0,nullptr),"Cancel completion failed");
    require(evaluate(engine,"fileEvents.join(',')","file-events.js").find("change,cancel")!=std::string::npos,"Cancellation delivered a change or was lost");
    pointer_button(engine,WEBSCENE_INPUT_POINTER_DOWN,400,300,7001U,true);
    pointer_button(engine,WEBSCENE_INPUT_POINTER_UP,400,300,7002U,false);
    execute(engine,R"JS(
      const bytes=new Uint8Array([99,0,1,255,65,99]);
      const blob=new Blob([bytes.subarray(1,5)],{type:'application/octet-stream'});
      const url=URL.createObjectURL(blob);
      // A Blob can back both a native Save request and a binary fetch. Revoking
      // its URL must invalidate future fetches without losing either byte copy.
      fetch(url).then(r=>r.arrayBuffer()).then(b=>globalThis.savedBlobFetch=Array.from(new Uint8Array(b))).catch(e=>globalThis.savedBlobFetch=String(e));
      const a=document.createElement('a');a.href=url;a.download='../café.bin';a.click();URL.revokeObjectURL(url);
      fetch(url).then(()=>globalThis.revokedBlobRejected=false,()=>globalThis.revokedBlobRejected=true);
    )JS","native-file-save.js");
    const auto save_request=take_download_request(engine);
    require(save_request.source_kind==WEBSCENE_DOWNLOAD_SOURCE_BYTES_V1
        && save_request.suggested_name=="café.bin"
        && save_request.mime_type=="application/octet-stream"
        && save_request.bytes==std::vector<uint8_t>({0,1,255,65})
        && save_request.total_size==4,
        "Save bytes, typed array slice, MIME, size, or name sanitization failed");
    require(evaluate(engine,"JSON.stringify(globalThis.savedBlobFetch)==='[0,1,255,65]'","saved-blob-fetch.js")=="true",
        "Native Save lost the binary Blob fetch payload");
    require(evaluate(engine,"revokedBlobRejected","revoked-blob-fetch.js")=="true",
        "Revoked native Save Blob URL remained fetchable");
    require(!webscene_engine_complete_file_request_v1(engine,0,0,nullptr,0,nullptr),"Invalid completion accepted");
    execute(engine,"(()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([]));a.download='empty.bin';a.click();URL.revokeObjectURL(a.href);})()","empty-save.js");
    const auto empty_request=take_download_request(engine);
    require(empty_request.source_kind==WEBSCENE_DOWNLOAD_SOURCE_BYTES_V1
        && empty_request.bytes.empty() && empty_request.total_size==0,
        "Empty export changed bytes");
    // A completion from a previous document must never populate the next one.
    execute(engine,"document.getElementById('native-file').click()","before-navigation.js");
    request=take();const auto old_id=request->request_id;webscene_file_request_release_v1(request);
    const auto path=std::filesystem::temp_directory_path()/("webscene-file-navigation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".html");
    {std::ofstream file(path);file<<"<!doctype html><input id='after' type='file'><script>globalThis.navigationReady=true;globalThis.unexpectedChange=false;document.getElementById('after').onchange=()=>unexpectedChange=true</script>";}
    const auto root=path.parent_path().string();webscene_engine_set_resource_root(engine,root.data(),root.size());
    const auto url="file://"+path.string();
    require(webscene_engine_load_url(engine,url.data(),url.size()),"File navigation failed");
    for(int i=0;i<100 && evaluate(engine,"globalThis.navigationReady===true","navigation-ready.js")!="true";++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    require(webscene_engine_complete_file_request_v1(engine,old_id,0,files,2,nullptr),"Stale completion could not be queued");
    require(evaluate(engine,"globalThis.unexpectedChange","after-navigation.js")=="false","Old file completion reached the new document");
    std::filesystem::remove(path);
    webscene_engine_enable_file_service_v1(engine,0);
}
