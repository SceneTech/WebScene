# Connected HTTP status boundary

Issue: #870

Document-owned resources accept response bytes only after the host callback reports transport success and, when HTTP metadata is present, a final status from 200 through 299. A 304 response remains valid only through the existing cached-body revalidation path. Other HTTP statuses fail the connected resource without evaluating, parsing, or caching their bodies.

Fetch keeps its separate contract: an HTTP 404 is a fulfilled `Response` whose status and body remain available to script. Non-HTTP/custom loaders that omit HTTP metadata retain their existing success behavior.

The native `webscene_native_connected_http_status` contract loads the same 404 body as a connected script and through Fetch, plus a 500 stylesheet. It requires one script error, no script load or body execution, no stylesheet cascade, and a fulfilled Fetch response carrying status 404 and the original body.
