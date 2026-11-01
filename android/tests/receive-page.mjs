// Exercise the shipped inline script without network or browser dependencies.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';
const java = readFileSync(new URL('../app/src/main/java/org/freetunnel/android/LinkReceiver.java', import.meta.url), 'utf8');
const literals = java.split('static final String SCRIPT = String.join("\\n",')[1].split('static String scriptHash()')[0];
const script = literals.trim().replace(/\);$/, '').split('\n').map(line => JSON.parse(line.trim().replace(/,$/, ''))).join('\n');
const element = () => ({value: '', hidden: true, disabled: false, textContent: '', children: [], listeners: {},
  addEventListener(name, callback) { this.listeners[name] = callback; },
  replaceChildren() { this.children = []; }, append(child) { this.children.push(child); }});
const text = element(), files = element(), button = element(), result = element(), details = element(), form = element();
files.files = [];
form.dataset = {sent: 'Отправлено', retry: 'Invalid', large: 'Too large', network: 'Delivery unknown'};
form.querySelector = () => button;
form.reset = () => { text.value = ''; files.files = []; };
const timers = new Map(); let id = 0;
const calls = [];
let answer = async () => ({ok: true, text: async () => ''});
runInNewContext(script, {
  document: {querySelector: () => form, querySelectorAll: () => [], createElement: element,
    getElementById: key => ({link: text, file: files, result, results: details})[key]},
  location: {pathname: '/TEST1234'},
  FormData: class { append(key, value) { this[key] = value; } },
  DOMParser: class {parseFromString(body) {return {querySelector: () => ({textContent: body})};}},
  AbortController, TypeError, Error,
  setTimeout: (callback, delay) => {timers.set(++id, {callback, delay}); return id;}, clearTimeout: id => timers.delete(id),
  fetch: async (url, options) => {calls.push(options.body); return answer(url, options);}
});
const submit = () => form.listeners.submit({preventDefault() {}});
text.value = 'tt://one\n tt://two\t tt://three';
await submit();
assert.deepEqual(calls.map(c => c.link), ['tt://one', 'tt://two', 'tt://three']);
assert.equal(result.textContent, 'Отправлено'); assert.equal(result.hidden, false);
assert.equal(text.value, '');
[...timers.values()].find(t => t.delay === 4000).callback();
assert.equal(result.hidden, true);
calls.length = 0;
text.value = 'tt://good\ntt://invalid'; text.listeners.input();
answer = async (_url, {body}) => ({ok: body.link !== 'tt://invalid', text: async () => 'Invalid'});
await submit();
assert.equal(details.children[0].textContent, '1 — Отправлено');
assert.equal(details.children[1].textContent, '2 — Invalid');
calls.length = 0;
await submit();
assert.deepEqual(calls.map(c => c.link), ['tt://invalid']);
// Editing clears a stale status and starts a new attempt.
text.value = ''; text.listeners.input();
assert.equal(result.hidden, true); assert.equal(details.children.length, 0);
const first = {name: 'first.toml', size: 100}, second = {name: 'second.txt', size: 100};
files.files = [first, second]; files.listeners.change();
answer = async () => ({ok: true, text: async () => ''}); calls.length = 0;
await submit();
assert.deepEqual(calls.map(c => c.file), [first, second]);
// Oversized files are rejected locally; transport errors never claim success.
files.files = [{name: 'huge.toml', size: 1024 * 1024}]; files.listeners.change(); calls.length = 0;
await submit(); assert.equal(calls.length, 0); assert.match(details.children[0].textContent, /Too large/);
files.files = []; text.value = 'tt://one'; text.listeners.input();
answer = async () => {throw new TypeError('network unavailable');};
await submit(); assert.match(details.children[0].textContent, /Delivery unknown/);
assert.equal(button.disabled, false);
console.log('ok - multiple links/files, partial retry, expiring status, oversized files and network errors');
