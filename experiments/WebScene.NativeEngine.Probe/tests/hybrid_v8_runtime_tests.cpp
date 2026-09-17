#include "webscene_v8_runtime.h"
#include "webscene_native_dom.h"
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void test_web_crypto_secure_random_realms() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    require(runtime.initialize(), "Web Crypto runtime failed");
    require(runtime.execute(R"JS(
        if (typeof crypto !== 'object' || Object.prototype.toString.call(crypto) !== '[object Crypto]')
          throw Error('Crypto global shape changed');
        if (Object.prototype.toString.call(crypto.subtle) !== '[object SubtleCrypto]')
          throw Error('SubtleCrypto digest surface is unavailable');
        if (crypto.getRandomValues.name !== 'getRandomValues'
            || crypto.getRandomValues.length !== 1
            || crypto.randomUUID.name !== 'randomUUID'
            || crypto.randomUUID.length !== 0)
          throw Error('Crypto Web IDL method shape changed');
        const integerArrays = [
          new Int8Array(33), new Uint8Array(33), new Uint8ClampedArray(33),
          new Int16Array(33), new Uint16Array(33),
          new Int32Array(33), new Uint32Array(33),
          new BigInt64Array(33), new BigUint64Array(33)
        ];
        for (const array of integerArrays) {
          if (crypto.getRandomValues(array) !== array)
            throw Error('getRandomValues did not return its argument');
        }
        if (!integerArrays.slice(3, 7).some(array =>
            Array.from(new Uint8Array(array.buffer)).some(byte => byte !== 0)))
          throw Error('integer arrays were not filled as raw bytes');
        const empty = new Uint8Array(0);
        if (crypto.getRandomValues(empty) !== empty)
          throw Error('empty integer view was rejected');
        const offsetBacking = new Uint8Array(32);
        offsetBacking.fill(0x5a);
        crypto.getRandomValues(offsetBacking.subarray(8, 24));
        if (!offsetBacking.slice(0, 8).every(byte => byte === 0x5a)
            || !offsetBacking.slice(24).every(byte => byte === 0x5a)
            || offsetBacking.slice(8, 24).every(byte => byte === 0x5a))
          throw Error('getRandomValues did not respect view bounds');
        for (const invalid of [new Float32Array(1), new Float64Array(1), new DataView(new ArrayBuffer(1)), {}]) {
          let rejected = false;
          try { crypto.getRandomValues(invalid); } catch (error) { rejected = error instanceof TypeError; }
          if (!rejected) throw Error('non-integer view was accepted');
        }
        const oversized = new Uint8Array(65537);
        oversized.fill(0x5a);
        let quotaRejected = false;
        try { crypto.getRandomValues(oversized); }
        catch (error) {
          quotaRejected = error instanceof DOMException
            && error.name === 'QuotaExceededError';
        }
        if (!quotaRejected || oversized.some(byte => byte !== 0x5a))
          throw Error('quota rejection mutated or accepted the destination');
        crypto.getRandomValues(new Uint8Array(65536));
        const uuid = crypto.randomUUID();
        if (!/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(uuid))
          throw Error('randomUUID did not produce an RFC 4122 v4 UUID');
        if (crypto.randomUUID() === uuid) throw Error('randomUUID repeated a value');
        let illegalInvocation = false;
        try { const unbound = crypto.getRandomValues; unbound(new Uint8Array(1)); }
        catch (error) { illegalInvocation = error instanceof TypeError; }
        if (!illegalInvocation) throw Error('unbound Crypto method was accepted');
        let uuidIllegalInvocation = false;
        try { const unbound = crypto.randomUUID; unbound(); }
        catch (error) { uuidIllegalInvocation = error instanceof TypeError; }
        if (!uuidIllegalInvocation) throw Error('unbound randomUUID was accepted');

        const frame = document.createElement('iframe');
        document.body.appendChild(frame);
        const child = frame.contentDocument;
        child.open();
        child.write(`<script>
          const bytes = new Uint32Array(8);
          if (crypto.getRandomValues(bytes) !== bytes) throw Error('iframe random identity changed');
          if (!/^[0-9a-f-]{36}$/.test(crypto.randomUUID())) throw Error('iframe UUID unavailable');
          if (Object.prototype.toString.call(crypto.subtle) !== '[object SubtleCrypto]')
            throw Error('iframe digest surface is unavailable');
          globalThis.cryptoRealmPassed = true;
        <\/script>`);
        child.close();
    )JS", "webcrypto-secure-random-realms"), runtime.last_error().c_str());
    for (unsigned index = 0; index < 200; ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(runtime.execute(R"JS(
        if (!frame.contentWindow.cryptoRealmPassed) throw Error('iframe Crypto realm failed');
        const childCrypto = frame.contentWindow.crypto;
        const childBytes = new Uint32Array(8);
        if (childCrypto.getRandomValues(childBytes) !== childBytes)
          throw Error('iframe Crypto object was not retained');
    )JS", "webcrypto-secure-random-iframe-result"), runtime.last_error().c_str());
}

void test_web_crypto_digest_realms_and_errors() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    std::string result;
    runtime.register_compiled_template("digest-result", [&](auto& dom, const std::string& value) -> auto& {
        result = value;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "Web Crypto digest runtime failed");
    require(runtime.execute(R"JS(
      (async () => {
        const hex = value => Array.from(new Uint8Array(value), byte =>
          byte.toString(16).padStart(2, '0')).join('');
        if (typeof SubtleCrypto !== 'function'
            || Object.prototype.toString.call(crypto.subtle) !== '[object SubtleCrypto]'
            || crypto.subtle.digest.name !== 'digest'
            || crypto.subtle.digest.length !== 2)
          throw Error('SubtleCrypto Web IDL shape changed');
        for (const constructor of [SubtleCrypto, CryptoKey]) {
          let rejected = false;
          try { new constructor(); } catch (error) { rejected = error instanceof TypeError; }
          if (!rejected) throw Error(`${constructor.name} constructor was exposed`);
        }
        let illegal = false;
        try { const digest = crypto.subtle.digest; digest('SHA-256', new Uint8Array()); }
        catch (error) { illegal = error instanceof TypeError; }
        if (!illegal) throw Error('Unbound digest receiver was accepted');

        const abc = new TextEncoder().encode('abc');
        if (hex(await crypto.subtle.digest('SHA-256', abc)) !==
            'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
          throw Error('FIPS SHA-256 vector changed');
        if (hex(await crypto.subtle.digest({name:'sha-1'}, abc)) !==
            'a9993e364706816aba3e25717850c26c9cd0d89d')
          throw Error('FIPS SHA-1 vector changed');
        const backing = new Uint8Array([0xff, 0x61, 0x62, 0x63, 0xff]);
        if (hex(await crypto.subtle.digest('SHA-256', backing.subarray(1, 4))) !==
            'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
          throw Error('BufferSource offset was ignored');
        const copied = new Uint8Array(1024); copied.fill(0x5a);
        const beforeMutation = crypto.subtle.digest('SHA-256', copied);
        copied.fill(0);
        if (hex(await beforeMutation) !==
            'e8fb68ce4d4d002dba40c0a459d96807c96ded1c2fdefae3f56f8a0c06a4fecf')
          throw Error('Digest did not copy input at call time');

        for (const [algorithm, data, expected] of [
          ['SHA-384', abc, 'NotSupportedError'],
          ['SHA-256', {}, 'TypeError'],
          ['SHA-256', new Uint8Array(16 * 1024 * 1024 + 1), 'OperationError']
        ]) {
          let name = '';
          try { await crypto.subtle.digest(algorithm, data); } catch (error) { name = error.name; }
          if (name !== expected) throw Error(`Digest error mismatch: ${name} != ${expected}`);
        }

        const workerUrl = URL.createObjectURL(new Blob([`
          onmessage = async event => {
            try {
              const digest = await crypto.subtle.digest('SHA-256', event.data);
              postMessage(Array.from(new Uint8Array(digest)));
            } catch (error) { postMessage({error: String(error)}); }
          };`], {type:'text/javascript'}));
        const worker = new Worker(workerUrl);
        URL.revokeObjectURL(workerUrl);
        const workerBytes = await new Promise((resolve, reject) => {
          worker.onmessage = event => resolve(event.data);
          worker.onerror = event => reject(Error(event.message));
          worker.postMessage(abc);
        });
        worker.terminate();
        if (workerBytes.error || hex(Uint8Array.from(workerBytes)) !==
            'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
          throw Error(workerBytes.error || 'Worker digest vector changed');
        document.createCompiledTemplate('digest-result', 1);
      })().catch(error => document.createCompiledTemplate(
        'digest-result', `${error.name}: ${error.message}`));
    )JS", "webcrypto-digest-realms-errors"), runtime.last_error().c_str());
    for (unsigned index = 0; index < 5000 && result.empty(); ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(result == "1", result.empty() ? "Digest promise did not settle" : result.c_str());
    result.clear();
    require(runtime.execute(R"JS(
      const digestFrame = document.createElement('iframe');
      document.body.appendChild(digestFrame);
      const digestChild = digestFrame.contentDocument;
      digestChild.open();
      digestChild.write('<script>globalThis.digestRealmReady = true;<\/script>');
      digestChild.close();
    )JS", "webcrypto-digest-frame-create"), runtime.last_error().c_str());
    for (unsigned index = 0; index < 200; ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(runtime.execute(R"JS(
      (async () => {
        const hex = value => Array.from(new Uint8Array(value), byte =>
          byte.toString(16).padStart(2, '0')).join('');
        if (!digestFrame.contentWindow || !digestFrame.contentWindow.digestRealmReady)
          throw Error('Iframe digest realm did not hydrate');
        const abc = new TextEncoder().encode('abc');
        const childSubtle = digestFrame.contentWindow.crypto.subtle;
        if (hex(await childSubtle.digest('SHA-256', abc)) !==
            'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
          throw Error('Iframe digest vector changed');
        if (hex(await childSubtle.digest.call(crypto.subtle, 'SHA-256', abc)) !==
            'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
          throw Error('Cross-realm legitimate receiver was rejected');
        document.createCompiledTemplate('digest-result', 1);
      })().catch(error => document.createCompiledTemplate(
        'digest-result', `${error.name}: ${error.message}`));
    )JS", "webcrypto-digest-frame-result"), runtime.last_error().c_str());
    for (unsigned index = 0; index < 2000 && result.empty(); ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(result == "1", result.empty() ? "Iframe digest promise did not settle" : result.c_str());
}

void test_web_crypto_digest_shutdown_is_bounded() {
    const auto started = std::chrono::steady_clock::now();
    {
        webscene_native::native_document document;
        webscene_native::v8_dom_runtime runtime(document,
            []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
        require(runtime.initialize(), "Web Crypto shutdown runtime failed");
        require(runtime.execute(R"JS(
          for (let index = 0; index < 4; ++index)
            crypto.subtle.digest('SHA-256', new Uint8Array(16 * 1024 * 1024));
        )JS", "webcrypto-digest-shutdown"), runtime.last_error().c_str());
    }
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(3),
        "Digest cancellation blocked realm shutdown");
}

void test_web_crypto_aes_gcm_vectors_realms_and_bounds() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    std::string result;
    runtime.register_compiled_template("aes-result", [&](auto& dom, const std::string& value) -> auto& {
        result = value;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "Web Crypto AES-GCM runtime failed");
    require(runtime.execute(R"JS(
      (async () => {
        const bytes = hex => Uint8Array.from(hex.match(/../g), byte => parseInt(byte, 16));
        const hex = value => Array.from(new Uint8Array(value), byte =>
          byte.toString(16).padStart(2, '0')).join('');
        for (const [name, length] of [['generateKey',3],['importKey',5],['exportKey',2],['encrypt',3],['decrypt',3]]) {
          if (crypto.subtle[name].name !== name || crypto.subtle[name].length !== length)
            throw Error(`${name} Web IDL shape changed`);
        }
        const raw = new Uint8Array(16);
        const iv = new Uint8Array(12);
        const plain = new Uint8Array(16);
        const key = await crypto.subtle.importKey('raw', raw, 'AES-GCM', true, ['encrypt','decrypt']);
        if (!(key instanceof CryptoKey) || key.type !== 'secret' || !key.extractable
            || key.algorithm.name !== 'AES-GCM' || key.algorithm.length !== 128
            || key.usages.join(',') !== 'encrypt,decrypt')
          throw Error('AES-GCM CryptoKey metadata changed');
        const encrypted = await crypto.subtle.encrypt({name:'AES-GCM',iv}, key, plain);
        if (hex(encrypted) !== '0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a67b21257bddf')
          throw Error('NIST AES-GCM vector changed');
        if (hex(await crypto.subtle.decrypt({name:'aes-gcm',iv}, key, encrypted)) !== '00000000000000000000000000000000')
          throw Error('AES-GCM round trip changed');
        const exported = new Uint8Array(await crypto.subtle.exportKey('raw', key));
        if (hex(exported) !== '00000000000000000000000000000000') throw Error('raw export changed');
        const jwk = await crypto.subtle.exportKey('jwk', key);
        if (jwk.kty !== 'oct' || jwk.alg !== 'A128GCM' || jwk.k !== 'AAAAAAAAAAAAAAAAAAAAAA')
          throw Error('JWK export changed');
        const jwkKey = await crypto.subtle.importKey('jwk', jwk, 'AES-GCM', true, ['decrypt']);
        if (hex(await crypto.subtle.decrypt({name:'AES-GCM',iv}, jwkKey, encrypted)) !== hex(plain))
          throw Error('JWK import changed');
        const copiedPlain = bytes('00112233445566778899aabbccddeeff');
        const copyPending = crypto.subtle.encrypt({name:'AES-GCM',iv}, key, copiedPlain);
        copiedPlain.fill(0xff); iv.fill(0xff); raw.fill(0xff);
        const copiedCipher = await copyPending;
        const repeatedCipher = await crypto.subtle.encrypt(
          {name:'AES-GCM',iv:new Uint8Array(12)}, key,
          bytes('00112233445566778899aabbccddeeff'));
        if (hex(copiedCipher) !== hex(repeatedCipher))
          throw Error('AES-GCM did not copy arguments at call time');
        const privateKey = await crypto.subtle.generateKey({name:'AES-GCM',length:256}, false, ['encrypt']);
        let exportError = '';
        try { await crypto.subtle.exportKey('raw', privateKey); } catch (error) { exportError = error.name; }
        if (exportError !== 'InvalidAccessError') throw Error(`nonextractable export: ${exportError}`);
        let usageError = '';
        try { await crypto.subtle.decrypt({name:'AES-GCM',iv:new Uint8Array(12)}, privateKey, encrypted); }
        catch (error) { usageError = error.name; }
        if (usageError !== 'InvalidAccessError') throw Error(`usage rejection: ${usageError}`);
        const tampered = new Uint8Array(encrypted); tampered[tampered.length - 1] ^= 1;
        let authError = '';
        try { await crypto.subtle.decrypt({name:'AES-GCM',iv:new Uint8Array(12)}, key, tampered); }
        catch (error) { authError = error.name; }
        if (authError !== 'OperationError') throw Error(`authentication rejection: ${authError}`);
        for (const [algorithm, data, expected] of [
          [{name:'AES-GCM',iv:new Uint8Array()}, plain, 'OperationError'],
          [{name:'AES-GCM',iv:new Uint8Array(12),tagLength:48}, plain, 'OperationError'],
          [{name:'AES-GCM',iv:new Uint8Array(12)}, {}, 'TypeError'],
          [{name:'AES-GCM',iv:new Uint8Array(12)}, new Uint8Array(16*1024*1024+1), 'OperationError']
        ]) {
          let name = '';
          try { await crypto.subtle.encrypt(algorithm, key, data); } catch (error) { name = error.name; }
          if (name !== expected) throw Error(`AES-GCM error mismatch: ${name} != ${expected}`);
        }
        const frame = document.createElement('iframe'); document.body.appendChild(frame);
        const child = frame.contentDocument; child.open(); child.write('<script>globalThis.ready=true;<\\/script>'); child.close();
        await new Promise(resolve => setTimeout(resolve));
        const foreign = await frame.contentWindow.crypto.subtle.importKey(
          'raw', new Uint8Array(16), 'AES-GCM', false, ['encrypt']);
        let realmError = '';
        try { await crypto.subtle.encrypt({name:'AES-GCM',iv:new Uint8Array(12)}, foreign, plain); }
        catch (error) { realmError = error.name; }
        if (realmError !== 'InvalidAccessError') throw Error(`foreign realm key: ${realmError}`);
        const throughputKey = await crypto.subtle.generateKey({name:'AES-GCM',length:128}, false, ['encrypt']);
        const payload = new Uint8Array(256 * 1024);
        const started = performance.now();
        for (let index = 0; index < 16; ++index)
          await crypto.subtle.encrypt({name:'AES-GCM',iv:new Uint8Array(12)}, throughputKey, payload);
        if (performance.now() - started >= 8000) throw Error('4 MiB AES-GCM throughput exceeded bound');
        document.createCompiledTemplate('aes-result', 1);
      })().catch(error => document.createCompiledTemplate('aes-result', `${error.name}: ${error.message}`));
    )JS", "webcrypto-aes-gcm"), runtime.last_error().c_str());
    for (unsigned index = 0; index < 12000 && result.empty(); ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(result == "1", result.empty() ? "AES-GCM promise did not settle" : result.c_str());
}

void test_web_crypto_aes_gcm_shutdown_is_bounded() {
    const auto started = std::chrono::steady_clock::now();
    {
        webscene_native::native_document document;
        webscene_native::v8_dom_runtime runtime(document,
            []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
        require(runtime.initialize(), "AES-GCM shutdown runtime failed");
        require(runtime.execute(R"JS(
          (async () => {
            const key = await crypto.subtle.generateKey({name:'AES-GCM',length:256}, false, ['encrypt']);
            for (let index = 0; index < 3; ++index)
              crypto.subtle.encrypt({name:'AES-GCM',iv:new Uint8Array(12)}, key,
                new Uint8Array(16 * 1024 * 1024)).catch(() => {});
          })();
        )JS", "webcrypto-aes-gcm-shutdown"), runtime.last_error().c_str());
        for (unsigned index = 0; index < 100; ++index) {
            require(runtime.pump_task(), runtime.last_error().c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(4),
        "AES-GCM cancellation blocked realm shutdown");
}

void test_web_crypto_aes_gcm_short_lived_key_reclamation() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    std::string result;
    runtime.register_compiled_template("aes-key-reclamation-result", [&](auto& dom, const std::string& value) -> auto& {
        result = value;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "AES-GCM short-lived key runtime failed");
    require(runtime.execute(R"JS(
      (async () => {
        const serverKey = new Uint8Array(32);
        for (let index = 0; index < serverKey.length; ++index)
          serverKey[index] = (index * 17 + 29) & 255;
        const plain = new TextEncoder().encode('{"scenetech":"server-key-secret"}');
        const started = performance.now();
        for (let iteration = 0; iteration < 96; ++iteration) {
          // Generate and export a client half, derive/import a working key,
          // then encrypt a persisted secret.
          const clientKeyObject = await crypto.subtle.generateKey(
            {name:'AES-GCM',length:256}, true, ['encrypt','decrypt']);
          const clientKey = new Uint8Array(await crypto.subtle.exportKey('raw', clientKeyObject));
          const derived = clientKey.map((value, index) => value ^ serverKey[index]);
          const key = await crypto.subtle.importKey(
            'raw', derived, {name:'AES-GCM',length:256}, true, ['encrypt','decrypt']);
          const iv = crypto.getRandomValues(new Uint8Array(12));
          const cipher = new Uint8Array(await crypto.subtle.encrypt(
            {name:'AES-GCM',iv}, key, plain));

          // Recover nonzero-offset slices from the persisted
          // client-key/IV/ciphertext aggregate and import again.
          const persisted = new Uint8Array(32 + 12 + cipher.byteLength);
          persisted.set(clientKey, 0); persisted.set(iv, 32); persisted.set(cipher, 44);
          const storedClientKey = persisted.subarray(0, 32);
          const storedIv = persisted.subarray(32, 44);
          const storedCipher = persisted.subarray(44);
          const reopenedDerived = storedClientKey.map(
            (value, index) => value ^ serverKey[index]);
          const reopenedKey = await crypto.subtle.importKey(
            'raw', reopenedDerived, {name:'AES-GCM',length:256}, true,
            ['encrypt','decrypt']);
          const clear = new Uint8Array(await crypto.subtle.decrypt(
            {name:'AES-GCM',iv:storedIv}, reopenedKey, storedCipher));
          if (clear.byteLength !== plain.byteLength
              || !clear.every((value, index) => value === plain[index]))
            throw Error(`AES-GCM persisted round trip ${iteration} changed`);
        }
        if (performance.now() - started >= 8000)
          throw Error('96 AES-GCM persisted round trips exceeded bound');
        document.createCompiledTemplate('aes-key-reclamation-result', 1);
      })().catch(error => document.createCompiledTemplate(
        'aes-key-reclamation-result', `${error.name}: ${error.message}`));
    )JS", "webcrypto-aes-gcm-key-reclamation"), runtime.last_error().c_str());
    for (unsigned index = 0; index < 10000 && result.empty(); ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(result == "1",
        result.empty() ? "AES-GCM key reclamation promise did not settle" : result.c_str());
}

void test_web_crypto_aes_cbc_decrypt_vectors_and_errors() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    std::string result;
    runtime.register_compiled_template("cbc-result", [&](auto& dom, const std::string& value) -> auto& {
        result = value; return dom.create_element("span");
    });
    require(runtime.initialize(), "Web Crypto AES-CBC runtime failed");
    require(runtime.execute(R"JS(
      (async () => {
        const bytes = hex => Uint8Array.from(hex.match(/../g), byte => parseInt(byte,16));
        const hex = value => Array.from(new Uint8Array(value), byte => byte.toString(16).padStart(2,'0')).join('');
        const keyBytes = bytes('2b7e151628aed2a6abf7158809cf4f3c');
        const iv = bytes('000102030405060708090a0b0c0d0e0f');
        const ciphertext = bytes('7649abac8119b246cee98e9b12e9197d8964e0b149c10b7b682e6e39aaeb731c');
        const key = await crypto.subtle.importKey('raw', keyBytes, 'aes-cbc', true, ['decrypt']);
        if (key.algorithm.name !== 'AES-CBC' || key.algorithm.length !== 128
            || key.usages.join(',') !== 'decrypt') throw Error('AES-CBC key metadata changed');
        if (hex(await crypto.subtle.decrypt({name:'AES-CBC',iv}, key, ciphertext)) !==
            '6bc1bee22e409f96e93d7e117393172a') throw Error('NIST AES-CBC vector changed');
        const jwk = await crypto.subtle.exportKey('jwk', key);
        if (jwk.alg !== 'A128CBC' || jwk.k !== 'K34VFiiu0qar9xWICc9PPA') throw Error('AES-CBC JWK changed');
        const jwkKey = await crypto.subtle.importKey('jwk', jwk, 'AES-CBC', false, ['decrypt']);
        if (hex(await crypto.subtle.decrypt({name:'AES-CBC',iv}, jwkKey, ciphertext)) !==
            '6bc1bee22e409f96e93d7e117393172a') throw Error('AES-CBC JWK import changed');
        const copied = new Uint8Array(ciphertext);
        const pending = crypto.subtle.decrypt({name:'AES-CBC',iv}, key, copied);
        copied.fill(0); iv.fill(0); keyBytes.fill(0);
        if (hex(await pending) !== '6bc1bee22e409f96e93d7e117393172a')
          throw Error('AES-CBC did not copy arguments at call time');
        for (const [algorithm,data,expected] of [
          [{name:'AES-CBC',iv:new Uint8Array(15)},ciphertext,'OperationError'],
          [{name:'AES-CBC',iv:new Uint8Array(16)},new Uint8Array(15),'OperationError'],
          [{name:'AES-CBC',iv:new Uint8Array(16)},{},'TypeError']
        ]) {
          let name=''; try { await crypto.subtle.decrypt(algorithm,key,data); } catch(error) { name=error.name; }
          if (name!==expected) throw Error(`AES-CBC error mismatch: ${name} != ${expected}`);
        }
        const tampered = new Uint8Array(ciphertext); tampered[tampered.length-1] ^= 1;
        let padding=''; try { await crypto.subtle.decrypt({name:'AES-CBC',iv:new Uint8Array(16)},key,tampered); }
        catch(error) { padding=error.name; }
        if (padding!=='OperationError') throw Error(`AES-CBC padding rejection: ${padding}`);
        let usage=''; try { await crypto.subtle.importKey('raw',new Uint8Array(16),'AES-CBC',false,['encrypt']); }
        catch(error) { usage=error.name; }
        if (usage!=='SyntaxError') throw Error(`AES-CBC usage rejection: ${usage}`);
        document.createCompiledTemplate('cbc-result',1);
      })().catch(error=>document.createCompiledTemplate('cbc-result',`${error.name}: ${error.message}`));
    )JS", "webcrypto-aes-cbc"), runtime.last_error().c_str());
    for (unsigned index=0; index<5000 && result.empty(); ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(result=="1", result.empty()?"AES-CBC promise did not settle":result.c_str());
}

void test_web_crypto_hmac_vectors_and_bounds() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    std::string result;
    runtime.register_compiled_template("hmac-result", [&](auto& dom, const std::string& value) -> auto& {
        result=value; return dom.create_element("span");
    });
    require(runtime.initialize(), "Web Crypto HMAC runtime failed");
    require(runtime.execute(R"JS(
      (async()=>{
        const hex=value=>Array.from(new Uint8Array(value),byte=>byte.toString(16).padStart(2,'0')).join('');
        const keyBytes=new Uint8Array(20);keyBytes.fill(0x0b);
        const data=new TextEncoder().encode('Hi There');
        const key=await crypto.subtle.importKey('raw',keyBytes,{name:'HMAC',hash:'SHA-256'},true,['sign']);
        if(key.algorithm.name!=='HMAC'||key.algorithm.hash.name!=='SHA-256'||key.algorithm.length!==160
          ||key.usages.join(',')!=='sign')throw Error('HMAC CryptoKey metadata changed');
        if(hex(await crypto.subtle.sign('HMAC',key,data))!==
          'b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7')
          throw Error('RFC 4231 HMAC-SHA-256 vector changed');
        const sha1=await crypto.subtle.importKey('raw',keyBytes,{name:'HMAC',hash:'SHA-1'},false,['sign']);
        if(hex(await crypto.subtle.sign({name:'hmac'},sha1,data))!==
          'b617318655057264e28bc0b6fb378c8ef146be00')throw Error('RFC 2202 HMAC-SHA-1 vector changed');
        const jwk=await crypto.subtle.exportKey('jwk',key);
        if(jwk.alg!=='HS256'||jwk.k!=='CwsLCwsLCwsLCwsLCwsLCwsLCws')throw Error('HMAC JWK changed');
        const copied=new Uint8Array(1024);copied.fill(0x5a);
        const pending=crypto.subtle.sign('HMAC',key,copied);copied.fill(0);keyBytes.fill(0);
        const expected=hex(await crypto.subtle.sign('HMAC',key,new Uint8Array(1024).fill(0x5a)));
        if(hex(await pending)!==expected)throw Error('HMAC did not copy input at call time');
        for(const [algorithm,keyArg,input,expectedName] of [
          ['RSA-PSS',key,data,'NotSupportedError'],
          ['HMAC',key,{},'TypeError'],
          ['HMAC',key,new Uint8Array(16*1024*1024+1),'OperationError']
        ]){let name='';try{await crypto.subtle.sign(algorithm,keyArg,input);}catch(error){name=error.name;}
          if(name!==expectedName)throw Error(`HMAC error mismatch: ${name} != ${expectedName}`);}
        let empty='';try{await crypto.subtle.importKey('raw',new Uint8Array(),{name:'HMAC',hash:'SHA-256'},false,['sign']);}
        catch(error){empty=error.name;}if(empty!=='DataError')throw Error(`empty HMAC key: ${empty}`);
        let usage='';try{await crypto.subtle.importKey('raw',new Uint8Array(32),{name:'HMAC',hash:'SHA-256'},false,['verify']);}
        catch(error){usage=error.name;}if(usage!=='SyntaxError')throw Error(`HMAC usage: ${usage}`);
        const payload=new Uint8Array(256*1024),started=performance.now();
        for(let index=0;index<16;++index)await crypto.subtle.sign('HMAC',key,payload);
        if(performance.now()-started>=8000)throw Error('4 MiB HMAC throughput exceeded bound');
        const retained=[key,sha1];
        for(let index=0;index<254;++index)retained.push(await crypto.subtle.importKey(
          'raw',new Uint8Array(32),{name:'HMAC',hash:'SHA-256'},false,['sign']));
        let capacity='';try{await crypto.subtle.importKey(
          'raw',new Uint8Array(32),{name:'HMAC',hash:'SHA-256'},false,['sign']);}
        catch(error){capacity=error.name;}
        if(capacity!=='OperationError')throw Error(`CryptoKey capacity: ${capacity}`);
        document.createCompiledTemplate('hmac-result',1);
      })().catch(error=>document.createCompiledTemplate('hmac-result',`${error.name}: ${error.message}`));
    )JS", "webcrypto-hmac"), runtime.last_error().c_str());
    for(unsigned index=0;index<10000&&result.empty();++index){
        require(runtime.pump_task(),runtime.last_error().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(result=="1",result.empty()?"HMAC promise did not settle":result.c_str());
}

void test_compiled_template_shared_document() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    webscene_native::dom_node* constructed = nullptr;
    runtime.register_compiled_template("button", [&](auto& dom, const std::string& data) -> auto& {
        require(&dom == &document, "Template received a different document");
        require(data == R"({"label":"Native"})", "Structured template data changed");
        auto& node = dom.create_element("button");
        node.attributes["id"] = "native-button";
        node.id_attribute = "native-button";
        node.text_content = "Native";
        constructed = &node;
        return node;
    });
    bool duplicate_rejected = false;
    try { runtime.register_compiled_template("button", [](auto& dom, const auto&) -> auto& {
        return dom.body();
    }); } catch (const std::invalid_argument&) { duplicate_rejected = true; }
    require(duplicate_rejected, "Duplicate factory replaced registered template");
    require(runtime.initialize(), "Compiled template runtime failed");
    require(runtime.execute(R"JS(
        const decoder = new TextDecoder('windows-1252', {fatal:true});
        if(decoder.decode(new Uint8Array([0x41,0x80,0x81,0x91,0x9f,0xff])) !== 'A\u20ac\u0081\u2018\u0178\u00ff')
            throw Error('DXF Windows-1252 decoding mismatch');
    )JS", "windows-1252"), runtime.last_error().c_str());
    require(runtime.execute(R"JS(
        const nativeButton = document.createCompiledTemplate('button', {label:'Native'});
        document.body.appendChild(nativeButton);
        if(document.getElementById('native-button') !== nativeButton) throw Error('Identity lost');
        let clicks = 0;
        const listener = () => { clicks++; nativeButton.textContent = 'Clicked'; };
        nativeButton.addEventListener('click', listener);
        nativeButton.click();
        nativeButton.removeEventListener('click', listener);
        nativeButton.click();
        if(clicks !== 1) throw Error('Listener cleanup failed');
        nativeButton.focus();
        if(document.activeElement !== nativeButton) throw Error('Native template focus failed');
        const compatibility = document.createElement('section');
        compatibility.innerHTML = '<b id="runtime-html">Compatible</b>';
        document.body.appendChild(compatibility);
        if(!document.getElementById('runtime-html')) throw Error('Runtime HTML broken');
        const fragment = document.createDocumentFragment();
        const moved = document.createElement('button');
        fragment.appendChild(moved);
        compatibility.replaceChildren('before', fragment, 'after');
        if(fragment.childNodes.length || moved.parentNode !== compatibility || compatibility.childNodes.length !== 3)
            throw Error('replaceChildren did not flatten fragment and preserve text arguments');
        let cycleRejected = false;
        try { compatibility.replaceChildren(compatibility); } catch(e) { cycleRejected = true; }
        if(!cycleRejected || compatibility.childNodes.length !== 3) throw Error('Invalid replacement was destructive');
        compatibility.insertAdjacentHTML('beforeend', '<b id="runtime-html">Compatible</b>');

        let rejected = false;
        try { document.createCompiledTemplate('missing'); } catch(e) { rejected = true; }
        if(!rejected) throw Error('Missing template silently accepted');
    )JS", "compiled-template-shared-document"), runtime.last_error().c_str());
    require(constructed && constructed->children.size() == 1 && constructed->children.front()->text_content == "Clicked", "Native side cannot see JS mutation");
    require(document.find_by_id("runtime-html") != nullptr, "Native side cannot see runtime HTML");
    require(runtime.execute("nativeButton.remove();", "compiled-template-remove"), "Template removal failed");
    require(constructed->parent == nullptr, "Native side cannot see JS removal");
}

void test_blob_worker_source_lifetime() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    bool received = false;
    runtime.register_compiled_template("worker-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "42", "Worker result changed"); received = true;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "Blob worker runtime failed");
    require(runtime.execute(R"JS(
        const workerURL = URL.createObjectURL(new Blob([`
          onmessage = event => {
            const bytes = new Uint32Array(8);
            if (crypto.getRandomValues(bytes) !== bytes)
              throw Error('worker random identity changed');
            if (!/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(crypto.randomUUID()))
              throw Error('worker UUID unavailable');
            if (Object.prototype.toString.call(crypto.subtle) !== '[object SubtleCrypto]')
              throw Error('worker digest surface is unavailable');
            postMessage(event.data + 1);
          };`], {type:'text/javascript'}));
        const blobWorker = new Worker(workerURL);
        blobWorker.onmessage = e => document.createCompiledTemplate('worker-result',e.data);
        URL.revokeObjectURL(workerURL);
        let revokedRejected = false;
        try { new Worker(workerURL); } catch(e) { revokedRejected = true; }
        if(!revokedRejected) throw Error('Revoked worker URL accepted');
        blobWorker.postMessage(41);
    )JS", "blob-worker-lifetime"), runtime.last_error().c_str());
    for(unsigned i=0;i<2000 && !received;++i) {
        runtime.pump_task();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(received, "Worker lost source after URL revocation");
    require(runtime.execute("blobWorker.terminate()", "worker-cleanup"), runtime.last_error().c_str());
}

void test_worker_message_port_transfer_and_throughput() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    bool completed = false;
    runtime.register_compiled_template("worker-port-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "10000", "Worker MessagePort result changed");
        completed = true;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "Worker MessagePort runtime failed");
    require(runtime.execute(R"JS(
        const source = `
          onmessage = event => {
            const port = event.data;
            if (!(port instanceof MessagePort)) throw Error('Transferred value is not a MessagePort');
            if (typeof document !== 'undefined' || typeof importScripts !== 'function')
              throw Error('Worker global shape changed');
            const isWebWorker = typeof self === 'object' && self.constructor
              && self.constructor.name === 'DedicatedWorkerGlobalScope';
            if (!isWebWorker
                || !(self instanceof DedicatedWorkerGlobalScope)
                || !(self instanceof WorkerGlobalScope)
                || !(self instanceof EventTarget)
                || Object.prototype.toString.call(self) !== '[object DedicatedWorkerGlobalScope]')
              throw Error('DedicatedWorkerGlobalScope identity changed');
            port.onmessage = message => port.postMessage(message.data + 1);
            port.start();
          }`;
        const url = URL.createObjectURL(new Blob([source], {type:'text/javascript'}));
        const worker = new Worker(url, {name:'message-port-contract'});
        URL.revokeObjectURL(url);
        const channel = new MessageChannel();
        if (!(channel.port1 instanceof MessagePort) || !(channel.port1 instanceof EventTarget))
          throw Error('MessagePort interface identity changed');
        let count = 0;
        channel.port1.onmessage = event => {
          count = event.data;
          if (count < 10000) channel.port1.postMessage(count);
          else {
            worker.terminate();
            channel.port1.close();
            document.createCompiledTemplate('worker-port-result', count);
          }
        };
        worker.postMessage(channel.port2, [channel.port2]);
        channel.port2.postMessage('detached endpoint must be inert');
        channel.port1.postMessage(0);
    )JS", "worker-message-port-throughput"), runtime.last_error().c_str());
    const auto started = std::chrono::steady_clock::now();
    while (!completed
        && std::chrono::steady_clock::now() - started < std::chrono::seconds(12)) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::yield();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    require(completed, "Worker MessagePort throughput did not complete");
    require(elapsed < std::chrono::seconds(10), "10,000 MessagePort round trips exceeded 10 seconds");
}

void test_message_port_clone_and_queue_bounds() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    require(runtime.initialize(), "MessagePort bound runtime failed");
    require(runtime.execute(R"JS(
        const clonedChannel = new MessageChannel();
        const clone = structuredClone(
          {port: clonedChannel.port2},
          {transfer: [clonedChannel.port2]});
        if (!(clone.port instanceof MessagePort)) throw Error('structuredClone lost MessagePort identity');
        let duplicateRejected = false;
        try { structuredClone(clone.port, {transfer:[clone.port, clone.port]}); }
        catch (error) { duplicateRejected = error.name === 'DataCloneError'; }
        if (!duplicateRejected) throw Error('Duplicate MessagePort transfer was accepted');
        clone.port.close();
        clonedChannel.port1.close();

        const bounded = new MessageChannel();
        const oversized = new ArrayBuffer(16 * 1024 * 1024);
        let byteSaturated = false;
        try { bounded.port1.postMessage(oversized, [oversized]); }
        catch (error) { byteSaturated = error.name === 'QuotaExceededError'; }
        if (!byteSaturated || oversized.byteLength === 0)
          throw Error('MessagePort byte bound detached or accepted an oversized packet');
        for (let index = 0; index < 256; ++index) bounded.port1.postMessage(index);
        let saturated = false;
        try { bounded.port1.postMessage(256); }
        catch (error) { saturated = error.name === 'QuotaExceededError'; }
        if (!saturated) throw Error('MessagePort queue accepted an unbounded backlog');
        bounded.port1.close();
        bounded.port2.close();

        globalThis.closedPortDelivered = false;
        const closing = new MessageChannel();
        closing.port2.onmessage = () => { closedPortDelivered = true; };
        closing.port2.close();
        closing.port2.close();
        closing.port1.postMessage('must not be delivered');
        closing.port1.close();
    )JS", "message-port-clone-and-bounds"), runtime.last_error().c_str());
    require(runtime.execute(
        "if (closedPortDelivered) throw Error('Closed MessagePort received a queued message')",
        "message-port-close"), runtime.last_error().c_str());
}

void test_message_port_receiver_survives_gc() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    require(runtime.initialize(), "MessagePort receiver runtime failed");
    for (const auto setup : {
        "channel.port1.onmessage = () => { ++receivedMessages; }; return channel.port2;",
        "channel.port1.addEventListener('message', () => { ++receivedMessages; }); channel.port1.start(); return channel.port2;",
        "channel.port1.onmessage = () => { ++receivedMessages; }; return structuredClone(channel.port2, {transfer:[channel.port2]});"
    }) {
        require(runtime.execute(std::string(R"JS(
        globalThis.receivedMessages = 0;
        globalThis.schedulerPort = (() => {
          const channel = new MessageChannel();
    )JS") + setup + R"JS(
        })();
    )JS", "message-port-unreferenced-receiver"), runtime.last_error().c_str());
        runtime.notify_low_memory();
        require(runtime.execute("schedulerPort.postMessage('scheduled')",
            "message-port-after-gc"), runtime.last_error().c_str());
        for (unsigned task = 0; task < 8; ++task)
            require(runtime.pump_task(), runtime.last_error().c_str());
        require(runtime.execute(
            "if (receivedMessages !== 1) throw Error('Entangled receiver was lost during GC'); schedulerPort.close()",
            "message-port-receiver-result"), runtime.last_error().c_str());
    }
}

void test_message_port_binding_memory_is_bounded() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    require(runtime.initialize(), "MessagePort memory runtime failed");

    auto create_and_release_batch = [&] {
        require(runtime.execute(R"JS(
            (() => {
              for (let index = 0; index < 2048; ++index) {
                const channel = new MessageChannel();
                channel.port1.close();
                channel.port2.close();
              }
            })()
        )JS", "message-port-memory-batch"), runtime.last_error().c_str());
        runtime.notify_low_memory();
        require(runtime.pump_task(), runtime.last_error().c_str());
    };

    create_and_release_batch();
    const auto baseline = runtime.read_memory_metrics().used_heap_bytes;
    create_and_release_batch();
    create_and_release_batch();
    // Private peer edges must not turn discarded, unclosed pairs into native
    // roots. Reclaiming more than the binding capacity proves cycle collection.
    for (unsigned batch = 0; batch < 3; ++batch) {
        require(runtime.execute(R"JS(
            (() => {
              for (let index = 0; index < 2048; ++index) {
                const channel = new MessageChannel();
                channel.port1.onmessage = () => {};
                channel.port2.addEventListener('message', () => {});
              }
            })()
        )JS", "message-port-unrooted-cycles"), runtime.last_error().c_str());
        runtime.notify_low_memory();
        require(runtime.pump_task(), runtime.last_error().c_str());
    }
    const auto retained = runtime.read_memory_metrics().used_heap_bytes;
    require(
        retained <= baseline + 16U * 1024U * 1024U,
        "Released MessagePort batches retained more than 16 MiB");

    require(runtime.execute(R"JS(
        globalThis.closedSenders = [];
        for (let index = 0; index < 2048; ++index) {
          const channel = new MessageChannel();
          channel.port1.onmessage = () => {};
          channel.port2.close();
          closedSenders.push(channel.port2);
        }
    )JS", "message-port-close-releases-peer"), runtime.last_error().c_str());
    runtime.notify_low_memory();
    require(runtime.execute(R"JS(
        // The 2048 reachable closed senders must not also retain 2048 peers.
        globalThis.newChannels = [];
        for (let index = 0; index < 1024; ++index)
          newChannels.push(new MessageChannel());
        closedSenders = null;
        newChannels = null;
    )JS", "message-port-reuse-after-close"), runtime.last_error().c_str());
}

void test_iframe_worker_extension_host_port_bootstrap() {
    webscene_native::native_document document;
    const std::string root_url = "https://worker.test/index.html";
    const std::string root_html = R"HTML(
      <!doctype html><script>
      addEventListener('message', event => {
        if (event.data !== 'vscode.bootstrap.nls') return;
        const frame = document.querySelector('iframe');
        if (event.source !== frame.contentWindow || event.origin !== origin)
          throw Error('iframe message source or origin changed');
        const channel = new MessageChannel();
        channel.port1.onmessage = response =>
          document.createCompiledTemplate('iframe-port-result', response.data);
        event.source.postMessage(
          {type:'vscode.init', port:channel.port2}, '*', [channel.port2]);
        channel.port1.postMessage(41);
      });
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      const child = frame.contentDocument;
      child.open();
      child.write(`<!doctype html><script>
        if (typeof Worker !== 'function') {
          throw Error('Worker is not installed in the iframe realm');
        }
        onmessage = event => {
          if (event.origin !== origin || !(event.data.port instanceof MessagePort)
              || !(event.data.port instanceof EventTarget)
              || event.ports[0] !== event.data.port) {
            throw Error('iframe transfer or same-origin contract changed');
          }
          const port = event.data.port;
          port.onmessage = message => port.postMessage(message.data + 1);
          port.start();
        };
        parent.postMessage('vscode.bootstrap.nls', '*');
      <\/script>`);
      child.close();
      </script>
    )HTML";
    bool completed = false;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t kind, const std::string& url, const auto&, const std::string&, int64_t,
            webscene_native::v8_dom_runtime::resource_response& response) {
            if (kind != WEBSCENE_RESOURCE_DOCUMENT || url != root_url) return false;
            response.content = root_html;
            return true;
        });
    runtime.register_compiled_template("iframe-port-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "42", "iframe MessagePort response changed");
        completed = true;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "iframe MessagePort runtime failed");
    require(runtime.load_url(root_url), runtime.last_error().c_str());
    for (unsigned i = 0; i < 5000 && !completed; ++i) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(completed, "Code OSS-shaped iframe MessagePort bootstrap did not complete");
}

void test_editor_worker_rpc_and_ui_responsiveness() {
    webscene_native::native_document document;
    const std::string root_url = "https://worker.test/index.html";
    const std::string module_url = "https://worker.test/editor-worker-rpc.js";
    const std::string module_source = R"JS(
      let workerId = -1;
      globalThis.onmessage = event => {
        const message = event.data;
        if (!message || !message.vsWorker || message.type !== 0) return;
        const reply = (res, err) => postMessage({
          vsWorker: workerId < 0 ? message.vsWorker : workerId,
          seq: message.req, res, err, type: 1
        });
        if (message.method === '$initialize') {
          workerId = message.args[0];
          reply(undefined, undefined);
        } else if (message.method === '$computeStringDiff') {
          const [original, modified] = message.args;
          const deadline = performance.now() + 25;
          while (performance.now() < deadline) {}
          reply({edits:[{start:0,end:original.length,text:modified}]}, undefined);
        } else {
          reply(undefined, {$isError:true,name:'Error',
            message:`Missing method ${message.method} on worker thread`});
        }
      };
    )JS";
    bool completed = false;
    std::string result;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t kind, const std::string& url, const auto&, const std::string&, int64_t,
            webscene_native::v8_dom_runtime::resource_response& response) {
            if (kind == WEBSCENE_RESOURCE_DOCUMENT && url == root_url) {
                response.content = "<!doctype html>";
                return true;
            }
            if (kind == WEBSCENE_RESOURCE_SCRIPT && url == module_url) {
                response.content = module_source;
                return true;
            }
            return false;
        });
    runtime.register_compiled_template("editor-worker-result",
        [&](auto& dom, const std::string& value) -> auto& {
            result = value;
            completed = true;
            return dom.create_element("span");
        });
    require(runtime.initialize(), "editor worker RPC runtime failed");
    require(runtime.load_url(root_url), runtime.last_error().c_str());
    require(runtime.execute(R"JS(
      (() => {
        const source = `
          await import('https://worker.test/editor-worker-rpc.js');
          globalThis.postMessage({type:'vscode-worker-ready'});
        `;
        const url = URL.createObjectURL(new Blob([source],
          {type:'application/javascript'}));
        const worker = new Worker(url,
          {name:'editorWorkerService',type:'module'});
        URL.revokeObjectURL(url);
        let received = 0;
        let nextRequest = 0;
        let uiTicks = 0;
        const ticker = setInterval(() => ++uiTicks, 1);
        const pending = new Map();
        const send = (method, args) => new Promise((resolve, reject) => {
          const req = String(++nextRequest);
          pending.set(req, {resolve, reject});
          worker.postMessage({
            vsWorker:1,req,channel:'default',method,args,type:0
          });
        });
        worker.onerror = event => document.createCompiledTemplate(
          'editor-worker-result', `error:${event.message}`);
        worker.onmessage = async event => {
          ++received;
          if (event.data?.type === 'vscode-worker-ready') {
            try {
              await send('$initialize', [1]);
              const original = 'const value = 1;\n';
              const modified = 'const value = 2;\nconsole.log(value);\n';
              const response = await send('$computeStringDiff', [
                original,modified,{maxComputationTimeMs:5000},'advanced'
              ]);
              const edit = response.edits[0];
              const applied = original.slice(0,edit.start)
                + edit.text + original.slice(edit.end);
              let rejection = '';
              try { await send('$missingMethod', []); }
              catch (error) { rejection = error.message; }
              clearInterval(ticker);
              worker.terminate();
              const failures = [];
              if (applied !== modified) failures.push('computed edit did not apply');
              if (received !== 4) failures.push(`received ${received} responses`);
              if (!rejection.includes('Missing method $missingMethod'))
                failures.push(`rejection was '${rejection}'`);
              if (uiTicks === 0) failures.push('document task queue was starved');
              document.createCompiledTemplate('editor-worker-result',
                failures.length ? failures.join('; ') : 1);
            } catch (error) {
              document.createCompiledTemplate(
                'editor-worker-result', `error:${error.message}`);
            }
            return;
          }
          const reply = pending.get(event.data?.seq);
          if (!reply) return;
          pending.delete(event.data.seq);
          if (event.data.err) reply.reject(Error(event.data.err.message));
          else reply.resolve(event.data.res);
        };
        worker.postMessage('-please-ignore-');
      })();
    )JS", "editor-worker-rpc"), runtime.last_error().c_str());
    const auto started = std::chrono::steady_clock::now();
    while (!completed
        && std::chrono::steady_clock::now() - started < std::chrono::seconds(5)) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::yield();
    }
    require(completed, "Editor worker RPC exceeded five seconds");
    require(result == "1", result.c_str());
}

void test_nested_worker_blob_url_identity() {
    webscene_native::native_document document;
    bool finished = false;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t kind, const std::string& url, const auto&, const std::string&, int64_t,
            webscene_native::v8_dom_runtime::resource_response& response) {
            if (kind != WEBSCENE_RESOURCE_DOCUMENT
                || url != "https://worker.test/index.html") return false;
            response.content = "<!doctype html>";
            return true;
        });
    runtime.register_compiled_template("nested-worker-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "\"nested-source-sentinel\"",
            "Nested worker resolved a colliding Blob URL source");
        finished = true;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "Nested worker Blob URL runtime failed");
    require(runtime.load_url("https://worker.test/index.html"), runtime.last_error().c_str());
    require(runtime.execute(R"JS(
      const primarySource = `
        const nestedSource = 'postMessage("nested-source-sentinel")';
        const nestedUrl = URL.createObjectURL(
          new Blob([nestedSource], {type:'application/javascript'}));
        postMessage({type:'_newWorker', url:nestedUrl});
        await Promise.resolve();
      `;
      const primaryUrl = URL.createObjectURL(
        new Blob([primarySource], {type:'application/javascript'}));
      const primary = new Worker(primaryUrl, {type:'module'});
      primary.onerror = event => document.createCompiledTemplate(
        'nested-worker-result', event.message);
      primary.onmessage = event => {
        if (event.data.type !== '_newWorker') return;
        if (event.data.url === primaryUrl)
          throw Error('Nested worker Blob URL reused its parent identifier');
        const nested = new Worker(event.data.url);
        nested.onerror = error => document.createCompiledTemplate(
          'nested-worker-result', error.message);
        nested.onmessage = message => document.createCompiledTemplate(
          'nested-worker-result', message.data);
      };
    )JS", "nested-worker-blob-identity"), runtime.last_error().c_str());
    for (unsigned i=0;i<5000 && !finished;++i) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(finished, "Nested worker did not execute its own Blob source");
}

void test_worker_inherits_authenticated_cookie_jar() {
    webscene_native::native_document document;
    std::atomic<bool> authenticated_module_request{false};
    std::atomic<bool> cross_origin_module_request{false};
    std::atomic<bool> cross_origin_cookie_disclosed{false};
    bool finished = false;
    std::string result;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t kind, const std::string& url,
            const webscene_native::v8_dom_runtime::resource_request_context& context,
            const std::string&, int64_t,
            webscene_native::v8_dom_runtime::resource_response& response) {
            response.has_http_metadata = true;
            response.final_url = url;
            if (kind == WEBSCENE_RESOURCE_DOCUMENT
                && url == "https://cross-origin.test/setup.html") {
                response.content = "<!doctype html>";
                response.headers.emplace_back(
                    "set-cookie", "cross-secret=private; Path=/; HttpOnly; SameSite=None; Secure");
                return true;
            }
            if (kind == WEBSCENE_RESOURCE_DOCUMENT
                && url == "https://authenticated-worker.test/index.html") {
                response.content = "<!doctype html>";
                response.headers.emplace_back(
                    "set-cookie", "vscode-tkn=worker-secret; Path=/; HttpOnly; SameSite=Lax");
                return true;
            }
            if (kind == WEBSCENE_RESOURCE_SCRIPT
                && url == "https://authenticated-worker.test/authenticated-module.js") {
                authenticated_module_request.store(true, std::memory_order_relaxed);
                if (context.cookie.find("vscode-tkn=worker-secret") == std::string::npos) {
                    response.status = 403U;
                    response.status_text = "Forbidden";
                    response.content = "Forbidden.";
                } else {
                    response.content = "export const authenticated = true;";
                }
                return true;
            }
            if (kind == WEBSCENE_RESOURCE_SCRIPT
                && url == "https://cross-origin.test/module.js") {
                cross_origin_module_request.store(true, std::memory_order_relaxed);
                const auto disclosed = context.cookie.find("cross-secret=private")
                    != std::string::npos;
                cross_origin_cookie_disclosed.store(
                    disclosed, std::memory_order_relaxed);
                response.content = disclosed
                    ? "throw Error('cross-origin-cookie-leaked');"
                    : "export const crossOriginLoaded = true;";
                return true;
            }
            return false;
        });
    runtime.register_compiled_template("worker-cookie-result",
        [&](auto& dom, const std::string& value) -> auto& {
            result = value;
            finished = true;
            return dom.create_element("span");
        });
    require(runtime.initialize(), "Authenticated worker runtime failed");
    require(runtime.load_url("https://cross-origin.test/setup.html"),
        runtime.last_error().c_str());
    require(runtime.load_url("https://authenticated-worker.test/index.html"),
        runtime.last_error().c_str());
    require(runtime.execute(R"JS(
      const source = `
        await import('https://authenticated-worker.test/authenticated-module.js');
        await import('https://cross-origin.test/module.js');
        postMessage('authenticated-worker-module');
      `;
      const url = URL.createObjectURL(
        new Blob([source], {type:'application/javascript'}));
      const worker = new Worker(url, {type:'module'});
      URL.revokeObjectURL(url);
      worker.onmessage = event => {
        worker.terminate();
        document.createCompiledTemplate('worker-cookie-result', event.data);
      };
      worker.onerror = event => {
        worker.terminate();
        document.createCompiledTemplate(
          'worker-cookie-result', `error: ${event.message}`);
      };
    )JS", "authenticated-worker-cookie-jar"), runtime.last_error().c_str());
    const auto started = std::chrono::steady_clock::now();
    while (!finished
        && std::chrono::steady_clock::now() - started < std::chrono::seconds(5)) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::yield();
    }
    require(authenticated_module_request.load(std::memory_order_relaxed),
        "Worker did not request its authenticated module");
    require(cross_origin_module_request.load(std::memory_order_relaxed),
        "Worker did not request its cross-origin module");
    require(!cross_origin_cookie_disclosed.load(std::memory_order_relaxed),
        "Worker disclosed a target-matching cookie to a cross-origin module");
    require(result == "\"authenticated-worker-module\"",
        result.empty() ? "Authenticated worker did not complete" : result.c_str());
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(4),
        "Authenticated worker startup exceeded four seconds");
}

void test_object_url_registry_capacity_and_reuse() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    require(runtime.initialize(), "Object URL capacity runtime failed");
    const auto started = std::chrono::steady_clock::now();
    require(runtime.execute(R"JS(
      const urls = [];
      for (let index = 0; index < 4096; ++index) {
        urls.push(URL.createObjectURL(new Blob(['x'])));
      }
      let bounded = false;
      try { URL.createObjectURL(new Blob(['overflow'])); }
      catch (error) { bounded = error.name === 'QuotaExceededError'; }
      if (!bounded) throw Error('Object URL registry accepted an unbounded entry count');
      for (const url of urls) URL.revokeObjectURL(url);
      const reused = URL.createObjectURL(new Blob(['reused']));
      URL.revokeObjectURL(reused);
    )JS", "object-url-capacity"), runtime.last_error().c_str());
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(5),
        "Bounded object URL registry operations exceeded five seconds");
}

void test_worker_termination_race() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    require(runtime.initialize(), "Worker termination runtime failed");
    require(runtime.execute(R"JS(
        const url = URL.createObjectURL(new Blob(['while(true) {}'], {type:'text/javascript'}));
        globalThis.terminatingWorker = new Worker(url);
        URL.revokeObjectURL(url);
    )JS", "worker-termination-start"), runtime.last_error().c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto started = std::chrono::steady_clock::now();
    require(runtime.execute(R"JS(
        for (let index = 0; index < 256; ++index) terminatingWorker.postMessage(index);
        let saturated = false;
        try { terminatingWorker.postMessage(256); }
        catch (error) { saturated = error.name === 'QuotaExceededError'; }
        if (!saturated) throw Error('Worker queue accepted an unbounded backlog');
        terminatingWorker.terminate();
        terminatingWorker.terminate();
    )JS",
        "worker-termination-stop"), runtime.last_error().c_str());
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
        "Worker termination did not cancel active execution promptly");
}

void test_worker_error_delivery() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    unsigned delivered = 0;
    runtime.register_compiled_template("worker-error-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "1", "worker error event shape changed");
        ++delivered;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "worker error runtime failed");
    require(runtime.execute(R"JS(
        const url = URL.createObjectURL(new Blob(
          ['throw new Error("expected-worker-failure")'],
          {type:'text/javascript'}));
        const failingWorker = new Worker(url);
        URL.revokeObjectURL(url);
        failingWorker.onerror = event => {
          document.createCompiledTemplate(
            'worker-error-result',
            event.type === 'error' && event.message.includes('expected-worker-failure') ? 1 : 0);
          failingWorker.terminate();
        };

        const moduleUrl = URL.createObjectURL(new Blob(
          ['export const broken = ;'],
          {type:'text/javascript'}));
        const failingModuleWorker = new Worker(moduleUrl, {type:'module'});
        URL.revokeObjectURL(moduleUrl);
        failingModuleWorker.onerror = event => {
          document.createCompiledTemplate(
            'worker-error-result', event.type === 'error' ? 1 : 0);
          failingModuleWorker.terminate();
        };
    )JS", "worker-error-delivery"), runtime.last_error().c_str());
    for (unsigned index = 0; index < 2000 && delivered < 2U; ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        if (!runtime.has_pending_tasks()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(delivered == 2U, "Classic and module worker failures did not dispatch error events");
}

void test_worker_and_port_navigation_shutdown() {
    webscene_native::native_document document;
    const std::string navigation_url = "https://worker.test/after-navigation.html";
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t kind, const std::string& url, const auto&, const std::string&, int64_t,
            webscene_native::v8_dom_runtime::resource_response& response) {
            if (kind != WEBSCENE_RESOURCE_DOCUMENT || url != navigation_url) return false;
            response.content = "<!doctype html><title>after navigation</title>";
            return true;
        });
    require(runtime.initialize(), "worker navigation runtime failed");
    require(runtime.execute(R"JS(
        const url = URL.createObjectURL(new Blob(['while(true) {}'], {type:'text/javascript'}));
        globalThis.navigationWorker = new Worker(url);
        URL.revokeObjectURL(url);
        globalThis.navigationChannel = new MessageChannel();
        navigationChannel.port1.postMessage('queued before navigation');
    )JS", "worker-navigation-start"), runtime.last_error().c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto started = std::chrono::steady_clock::now();
    require(runtime.load_url(navigation_url), runtime.last_error().c_str());
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
        "Navigation did not cancel worker execution promptly");
    require(runtime.execute(R"JS(
        const afterNavigation = new MessageChannel();
        afterNavigation.port1.close();
        afterNavigation.port2.close();
    )JS", "worker-navigation-resources-reset"), runtime.last_error().c_str());
}

void test_window_messageerror_on_receiver_resource_exhaustion() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    bool completed = false;
    runtime.register_compiled_template("messageerror-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "1", "iframe received the wrong structured-clone failure event");
        completed = true;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "window messageerror runtime failed");
    require(runtime.execute(R"JS(
        addEventListener('message', event => {
          if (event.data !== 'messageerror-ready') return;
          const channels = [];
          for (let index = 0; index < 2048; ++index) channels.push(new MessageChannel());
          globalThis.__webSceneMessageErrorChannels = channels;
          const transferred = channels[channels.length - 1].port2;
          event.source.postMessage({transferred}, '*', [transferred]);
        });
        const frame = document.createElement('iframe');
        document.body.appendChild(frame);
        const child = frame.contentDocument;
        child.open();
        child.write(`<script>
          onmessageerror = event =>
            document.createCompiledTemplate('messageerror-result', event.type === 'messageerror' ? 1 : 0);
          parent.postMessage('messageerror-ready', '*');
        <\/script>`);
        child.close();
    )JS", "window-messageerror-resource-bound"), runtime.last_error().c_str());
    for (unsigned index = 0; index < 32 && !completed; ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
    }
    require(completed, "Window did not dispatch messageerror after clone reconstruction failed");
}

void test_compiled_document_lifecycle(bool strict) {
    webscene_native::native_document document;
    unsigned document_requests = 0;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t kind, const std::string&, const auto&, const std::string&, int64_t, auto&) {
            if (kind == WEBSCENE_RESOURCE_DOCUMENT) ++document_requests;
            return false;
        });
    require(runtime.initialize(), "Compiled root runtime failed");
    webscene_native::v8_dom_runtime::compiled_document package;
    package.allow_runtime_html = !strict;
    package.base_url = "https://compiled.test/app/index.html";
    package.construct = [](auto& dom) {
        auto& html = dom.create_element("html");
        auto& head = dom.create_element("head");
        auto& body = dom.create_element("body");
        auto& button = dom.create_element("button");
        button.id_attribute = "compiled-root-button";
        button.attributes["id"] = button.id_attribute;
        dom.append_child(dom.body(), html);
        dom.append_child(html, head);
        dom.append_child(html, body);
        dom.append_child(body, button);
    };
    package.templates.emplace("parts", [](auto& dom, webscene_native::compiled_template_arguments args) -> auto& {
        auto& fragment = dom.create_element("#document-fragment");
        auto& span = dom.create_element("span");
        webscene_native::compiled_append_argument(dom, span, webscene_native::compiled_argument(args, 0));
        dom.append_child(fragment, span);
        return fragment;
    });
    package.scripts.push_back({{}, R"JS(
        if(!document.getElementById('compiled-root-button')) throw Error('Scripts ran before construction');
        if(document.readyState !== 'loading') throw Error('Incorrect initial lifecycle');
        globalThis.lifecycle = ['script'];
        document.addEventListener('DOMContentLoaded', () => lifecycle.push('dom'));
        window.addEventListener('load', () => lifecycle.push('load'));
    )JS"});
    require(runtime.load_compiled_document(package), runtime.last_error().c_str());
    require(document_requests == 0, "Compiled navigation fetched an HTML shell");
    require(runtime.execute(R"JS(
        const input = document.createDocumentFragment();
        const child = document.createElement('em'); child.textContent = 'native parts';
        input.appendChild(child);
        const output = document.createCompiledTemplateParts('parts', [input]);
        if(input.childNodes.length || output.firstChild.firstChild !== child) throw Error('Native parts lost node identity');
        document.body.appendChild(output);
        if(!child.isConnected || output.childNodes.length) throw Error('Native parts not attached');
        let rejected = false;
        try { document.createCompiledTemplateParts('parts', [child]); } catch(e) { rejected = true; }
        if(!rejected || !child.isConnected) throw Error('Attached argument was moved or accepted');
        if(lifecycle.join(',') !== 'script,dom,load') throw Error('Lifecycle order');
        if(document.readyState !== 'complete') throw Error('Readiness incomplete');
        if(location.href !== 'https://compiled.test/app/index.html') throw Error('Base URL lost');

    )JS", "compiled-document-lifecycle"), runtime.last_error().c_str());
    if (strict) {
        require(runtime.execute(R"JS(
            const control = document.getElementById('compiled-root-button');
            control.textContent = 'Preserved';
            const attempts = [
                () => { control.innerHTML = '<b>Forbidden</b>'; },
                () => control.insertAdjacentHTML('beforeend', '<b>Forbidden</b>'),
                () => new DOMParser().parseFromString('<b>Forbidden</b>', 'text/html'),
                () => document.createRange().createContextualFragment('<b>Forbidden</b>')
            ];
            for(const attempt of attempts) {
                let rejected = false;
                try { attempt(); } catch(e) { rejected = String(e).includes('Runtime HTML'); }
                if(!rejected) throw Error('Strict parser entry not rejected');
                if(control.textContent !== 'Preserved') throw Error('Rejected parse mutated existing content');
            }
            control.innerHTML = '';
            if(control.textContent !== '') throw Error('Empty clear should not require parsing');
        )JS", "compiled-document-strict"), runtime.last_error().c_str());
        require(!runtime.load_url("https://compiled.test/runtime.html"), "Strict document allowed HTML navigation");
    } else {
        require(runtime.execute("document.getElementById('compiled-root-button').innerHTML = '<span>Compatible</span>';",
            "compiled-document-compatible"), runtime.last_error().c_str());
    }
}

void test_worker_message_port_contracts() {
    test_message_port_receiver_survives_gc();
    test_worker_message_port_transfer_and_throughput();
    test_message_port_clone_and_queue_bounds();
    test_message_port_binding_memory_is_bounded();
    test_iframe_worker_extension_host_port_bootstrap();
    test_editor_worker_rpc_and_ui_responsiveness();
    test_worker_termination_race();
    test_worker_error_delivery();
    test_worker_and_port_navigation_shutdown();
    test_window_messageerror_on_receiver_resource_exhaustion();
    test_nested_worker_blob_url_identity();
    test_worker_inherits_authenticated_cookie_jar();
    test_object_url_registry_capacity_and_reuse();
}

int main() {
    try {
        if (const auto* filter = std::getenv(
                "WEBSCENE_HYBRID_V8_RUNTIME_TEST_FILTER");
            filter != nullptr) {
            const auto selected = std::string_view(filter);
            if (selected == "webcrypto-secure-random") {
                test_web_crypto_secure_random_realms();
                return 0;
            }
            if (selected == "webcrypto-digest") {
                test_web_crypto_digest_realms_and_errors();
                test_web_crypto_digest_shutdown_is_bounded();
                return 0;
            }
            if (selected == "webcrypto-aes-gcm") {
                test_web_crypto_aes_gcm_vectors_realms_and_bounds();
                test_web_crypto_aes_gcm_shutdown_is_bounded();
                test_web_crypto_aes_gcm_short_lived_key_reclamation();
                return 0;
            }
            if (selected == "webcrypto-aes-cbc") {
                test_web_crypto_aes_cbc_decrypt_vectors_and_errors();
                return 0;
            }
            if (selected == "webcrypto-hmac") {
                test_web_crypto_hmac_vectors_and_bounds();
                return 0;
            }
            if (selected == "worker-messageport") {
                test_blob_worker_source_lifetime();
                test_worker_message_port_contracts();
                return 0;
            }
            if (selected == "messageport-gc") {
                test_message_port_receiver_survives_gc();
                return 0;
            }
        }
        test_web_crypto_secure_random_realms();
        test_web_crypto_digest_realms_and_errors();
        test_web_crypto_digest_shutdown_is_bounded();
        test_web_crypto_aes_gcm_vectors_realms_and_bounds();
        test_web_crypto_aes_gcm_shutdown_is_bounded();
        test_web_crypto_aes_gcm_short_lived_key_reclamation();
        test_web_crypto_aes_cbc_decrypt_vectors_and_errors();
        test_web_crypto_hmac_vectors_and_bounds();
        test_blob_worker_source_lifetime();
        test_worker_message_port_contracts();
        test_compiled_template_shared_document();
        test_compiled_document_lifecycle(false);
        test_compiled_document_lifecycle(true);
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
