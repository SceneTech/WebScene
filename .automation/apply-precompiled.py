"""One-time, base-blob-checked integration on the precompiled feature branch only.
Removed together with its branch-only workflow before the implementation commit.
"""
import hashlib
from pathlib import Path

base = 'experiments/WebScene.NativeEngine.Probe/'
expected = {
 base+'native/webscene_v8_runtime.cpp': 'a5ced2524902ca2e8869d49c1033cab57be1ec60',
 base+'native/webscene_v8_runtime_cache_and_frames.inc': '6618de41d774999fff46ba9e512249f084459ab1',
 base+'native/webscene_v8_runtime_modules.inc': None,
 base+'CMakeLists.txt': '04b3726fd1e93db103aad7627f1cd8c73b1a40fc',
 base+'native/webscene_native_engine.exports': '0306fdf335796713d3827ddc9e90912e05e8cc23',
 'src/WebScene.Sdk/CMakeLists.txt': 'd788850fd46d04eb38cd606d0574fc0d66887ded',
 'src/WebScene.Sdk/cmake/WebSceneConfig.cmake': 'bd3e3287e105f5f21af64aa47dccdac493223bfb',
}
texts = {}
for name, sha in expected.items():
    raw = Path(name).read_bytes()
    actual = hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    if sha and sha != actual:
        raise RuntimeError(f'Base file changed: {name}: {actual}')
    texts[name] = raw.decode()
def change(name, before, after):
    assert texts[name].count(before) == 1, f'Expected exactly one integration point in {name}: {before[:100]}'
    texts[name] = texts[name].replace(before, after, 1)

runtime = base+'native/webscene_v8_runtime.cpp'
change(runtime, '#include "webscene_v8_runtime.h"', '#include "webscene_v8_runtime.h"\n#include "webscene_precompiled_javascript.h"')
change(runtime, '#include "webscene_v8_runtime_support.inc"\n} // namespace',
 '''#include "webscene_v8_runtime_support.inc"
#include "webscene_precompiled_javascript_support.inc"
} // namespace

#include "webscene_precompiled_javascript_api.inc"''')

cache = base+'native/webscene_v8_runtime_cache_and_frames.inc'
marker = '        const auto process_acquisition = acquire_process_compilation(key);'
change(cache, marker, '''        // Packaged caches are immutable and independent of installed resource URLs.
        // Preserve the existing local/shared caches above and persistent path below.
        if (const auto packaged = find_precompiled_javascript(source, false)) {
            const auto source_value = create_script_source_value(key, source, std::move(immutable_source));
            const auto name_value = v8::String::NewFromUtf8(isolate, document_name.data(),
                v8::NewStringType::kNormal, static_cast<int>(document_name.size())).ToLocalChecked();
            v8::Local<v8::UnboundScript> unbound;
            const auto started = std::chrono::steady_clock::now();
            const auto accepted = consume_precompiled_classic(isolate, source_value, name_value, *packaged, unbound);
            compilation_time_nanosecond_count += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
            if (!accepted) return false;
            retain_shared_compiled_script(key, key_digest, unbound, source.size());
            retain_compiled_script(key, key_digest, unbound, source.size());
            script = unbound->BindToCurrentContext();
            return true;
        }

'''+marker)
modules = base+'native/webscene_v8_runtime_modules.inc'
change(modules, '''        v8::ScriptOrigin origin(js_dom_string(isolate, url), 0, 0, false, -1, {}, false, false, true);
        v8::ScriptCompiler::Source input(js_dom_string(isolate, std::string(source)), origin);
        v8::Local<v8::Module> result;
        if (!v8::ScriptCompiler::CompileModule(isolate, &input).ToLocal(&result)) return {};''',
'''        v8::Local<v8::Module> result;
        if (const auto packaged = find_precompiled_javascript(source, true)) {
            if (!consume_precompiled_module(isolate, js_dom_string(isolate, std::string(source)),
                    js_dom_string(isolate, url), *packaged, result)) return {};
        } else {
            v8::ScriptOrigin origin(js_dom_string(isolate, url), 0, 0, false, -1, {}, false, false, true);
            v8::ScriptCompiler::Source input(js_dom_string(isolate, std::string(source)), origin);
            if (!v8::ScriptCompiler::CompileModule(isolate, &input).ToLocal(&result)) return {};
        }''')
texts[base+'CMakeLists.txt'] += '\ninclude("${CMAKE_CURRENT_LIST_DIR}/cmake/PrecompiledJavaScript.cmake")\n'
texts[base+'native/webscene_native_engine.exports'] += '\n_webscene_register_precompiled_javascript_v1\n_webscene_get_precompiled_javascript_stats_v1\n_webscene_precompile_javascript_v1\n'
texts['src/WebScene.Sdk/CMakeLists.txt'] += '''
# The host compiler and its initialization assets are SDK tools, never app payloads.
install(TARGETS webscene-jsc RUNTIME DESTINATION bin)
install(FILES "${WEBSCENE_ROOT}/experiments/WebScene.NativeEngine.Probe/native/webscene_precompiled_javascript.h" DESTINATION include)
install(PROGRAMS tools/precompile_javascript.py DESTINATION share/webscene/tools)
install(FILES "${WEBSCENE_V8_OUTPUT_ROOT}/icudtl.dat"
  "${CMAKE_BINARY_DIR}/runtime/webscene_bootstrap_snapshot.bin"
  "${CMAKE_BINARY_DIR}/runtime/webscene_bootstrap_snapshot.meta" DESTINATION bin)
'''
texts['src/WebScene.Sdk/cmake/WebSceneConfig.cmake'] += '''
if(EXISTS "${_ws_prefix}/bin/webscene-jsc")
  add_executable(WebScene::JavaScriptCompiler IMPORTED GLOBAL)
  set_property(TARGET WebScene::JavaScriptCompiler PROPERTY IMPORTED_LOCATION "${_ws_prefix}/bin/webscene-jsc")
  set(WebScene_PrecompiledJavaScript_SUPPORTED TRUE)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/WebScenePrecompiledJavaScript.cmake")
'''
# Normalize the single transferred quote literal before checking source hashes.
tool = Path(base+'tools/precompile_javascript.cpp')
text = tool.read_text()
if '\u201c' in text:
    assert text.count('\u201c') == 1
    tool.write_text(text.replace('\u201c', '"'))
checks = {
 '.github/workflows/precompiled-javascript.yml': '6b5cf1dc7a8395e3eefadd67ee867d9850685e4d1528485ecdc08e43341384e5',
 'docs/guides/precompiled-javascript.md': 'b1e0fcb23140b3388897f9d08b9efc52668696bbb4329c683058e3f9aa37a60d',
 base+'cmake/PrecompiledJavaScript.cmake': '9e6466074e518aa6bc422e19dec282edd600a16feb3d16ced260aec2e5c7f54f',
 base+'native/webscene_precompiled_javascript.h': 'c2f6ca92e984fc2717a6bc10baeaf630b1624fec994e34e154d5d641aa97d703',
 base+'native/webscene_precompiled_javascript_api.inc': '8c8b243fa0a8ef15b9499852b7d16d4c000e36bfa5f8e667bf0d30dd391e23f1',
 base+'native/webscene_precompiled_javascript_stub.cpp': 'fc71edbd9e689ea8f422a7981d434230489491697e61434698e55daf4c3867b3',
 base+'native/webscene_precompiled_javascript_support.inc': '48537b6c9a2bc64933f3eeba529547a7b3d24d58fec6e2331cd86d603da46133',
 base+'tests/precompiled/native_engine_tests.cpp': 'f66d5cef937e8aeb3aeb40003b619653111187bb86691c2a96d1919ca2f3ce21',
 base+'tests/precompiled/node_adapter.cpp': '9dd57669b6c5c6e82591e3954a67a75d1b82571fb52d8128d32ece248a3ef1a7',
 base+'tests/precompiled/test_cmake.py': '22ee8fd2cdf353bec53da3747b27054a436b38f736c1cbddf2878a3129da4143',
 base+'tests/precompiled/test_driver.py': '69175efc6673d194661d9354ba5ca24c75bb0b8fe8d8b46c3373448b4b7b1cf7',
 base+'tests/precompiled/test_v8.cjs': 'fcc000e9d18c87836196b0fc713fe6605693ddb41f94289c03f107fc8a8a98c7',
 base+'tools/precompile_javascript.cpp': '3aa0d1f0350a13aee6381299f861945b76e92669f524cc22b69ec1e42b943ee0',
 'src/WebScene.Sdk/cmake/WebScenePrecompiledJavaScript.cmake': '47fc0e423b1eb4b0eb79ce64ffffb139b847fda12bd5f9ec00fe08b52c599ee9',
 'src/WebScene.Sdk/tools/precompile_javascript.py': 'ce1fe0bf8282afebd1e800cb452200b3190d2fc9690286e3e4edddf806a50208'
}
for name, expected_hash in checks.items():
    actual_hash = hashlib.sha256(Path(name).read_bytes()).hexdigest()
    assert actual_hash == expected_hash, f'Transferred source mismatch: {name}: {actual_hash}'
for name, text in texts.items():
    Path(name).write_text(text)
for name in ('.github/workflows/integrate-precompiled.yml', '.automation/apply-precompiled.py'):
    Path(name).unlink()
print(f'Integrated {len(checks)} new files and {len(texts)} existing files')
