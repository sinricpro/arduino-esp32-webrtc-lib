"""Opt-in live browser test of a flashed Doorbell on a trusted LAN.

python tests/hardware_browser.py http://BOARD_IP --token YOUR_TOKEN
Requires pip install playwright, Microsoft Edge, and the board already running.
"""
import argparse
import asyncio
import json
from pathlib import Path
from playwright.async_api import async_playwright

async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('url')
    parser.add_argument('--token', default='change-this-token')
    parser.add_argument('--seconds', type=int, default=20)
    parser.add_argument('--output', default='build/browser-test.json')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--expose-ice-addresses', action='store_true', help='Diagnostic only: disable mDNS hiding in this temporary browser')
    parser.add_argument('--all-interfaces', action='store_true', help='Test with a fake microphone to expose LAN interfaces when a VPN is the default route; records no real microphone')
    parser.add_argument('--serial-port', help='Optional board serial port; send r to verify ring notification (no reset)')
    args = parser.parse_args()
    async with async_playwright() as p:
        launch_args = ['--disable-features=WebRtcHideLocalIpsWithMdns'] if args.expose_ice_addresses else []
        if args.all_interfaces:
            launch_args += ['--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream',
                            '--unsafely-treat-insecure-origin-as-secure=' + args.url.rstrip('/')]
        browser = await p.chromium.launch(channel='msedge', headless=True, args=launch_args)
        context = await browser.new_context(permissions=['microphone'] if args.all_interfaces else [])
        page = await context.new_page()
        errors = []
        page.on('pageerror', lambda e: errors.append(str(e)))
        if args.debug:
            def request(req):
                if req.url.endswith('/offer'):
                    Path('build/browser-offer.sdp').write_text(req.post_data or '')
                print('HTTP', req.method, req.url, flush=True)
            page.on('request', request)
            page.on('response', lambda res: print('RESPONSE', res.status, res.url, flush=True))
            page.on('requestfailed', lambda req: print('FAILED', req.url, req.failure, flush=True))
        await page.goto(args.url)
        if args.all_interfaces:
            await page.evaluate("async()=>{window.testFakeStream=await navigator.mediaDevices.getUserMedia({audio:true});}")
        await page.fill('#token', args.token)
        await page.click('#connect')
        try:
            await page.wait_for_function("document.getElementById('accept').disabled === false", timeout=45000)
            await page.evaluate('''() => {
              window.testPackets=0;window.testBytes=0;window.testJpegs=0;
              dc.addEventListener('message', e=>{
                if(e.data instanceof ArrayBuffer){window.testPackets++;window.testBytes+=e.data.byteLength;}
              });
              const ctx=document.getElementById('view').getContext('2d');
              const original=ctx.drawImage.bind(ctx);
              ctx.drawImage=(...args)=>{window.testJpegs++;return original(...args);};
            }''')
            await page.click('#accept')
            await page.wait_for_timeout(args.seconds * 1000)
            result = await page.evaluate('''async () => {
              const stats=[];for(const s of (await pc.getStats()).values())
                if(['transport','candidate-pair','inbound-rtp','data-channel'].includes(s.type))stats.push(s);
              return {state:pc.connectionState,packets:window.testPackets,bytes:window.testBytes,
                jpegFrames:window.testJpegs,status:document.getElementById('status').textContent,stats};
            }''')
            await page.screenshot(path='build/browser-camera.png')
            await page.click('#door')
            await page.wait_for_function("document.getElementById('status').textContent === 'NO_LOCK_CONFIGURED'")
            result['doorCommand'] = 'PASS (no actuator configured)'
            if args.serial_port:
                import serial
                port = serial.Serial(port=None, baudrate=115200, timeout=1)
                port.dtr = False; port.rts = False; port.port = args.serial_port
                port.open(); port.write(b'r'); port.flush(); port.close()
                await page.wait_for_function("document.getElementById('status').textContent.includes('ringing')")
                result['ring'] = 'PASS'
            await page.click('#deny')
            await page.wait_for_timeout(400)
            before = await page.evaluate('window.testJpegs')
            await page.wait_for_timeout(1000)
            after = await page.evaluate('window.testJpegs')
            assert after - before <= 1, 'Camera continues after End call'
            result['endCall'] = 'PASS'
            await page.click('#disconnect')
            await page.wait_for_function("document.getElementById('connect').disabled === false")
            await page.wait_for_timeout(1000)
            await page.click('#connect')
            await page.wait_for_function("document.getElementById('accept').disabled === false", timeout=45000)
            await page.evaluate('window.testJpegs=0')
            await page.click('#accept')
            await page.wait_for_timeout(3000)
            result['reconnectFrames'] = await page.evaluate('window.testJpegs')
            assert result['reconnectFrames'] > 0, 'No frames after reconnect'
            await page.click('#disconnect')
            await page.wait_for_function("document.getElementById('connect').disabled === false")
            result['errors'] = errors
            Path(args.output).write_text(json.dumps(result, indent=2))
            print(json.dumps({k:v for k,v in result.items() if k!='stats'}, indent=2))
            assert result['state'] == 'connected', result
            assert result['jpegFrames'] > 0, 'No complete camera frames rendered'
            assert not errors, errors
        except Exception:
            print('Viewer status:', await page.locator('#status').inner_text())
            print('Browser errors:', errors)
            await page.screenshot(path='build/browser-failure.png')
            raise
        finally:
            await browser.close()

asyncio.run(main())
