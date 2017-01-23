"use strict";

let mockTextDetectionReady = define(
  'mockTextDetection',
  ['services/shape_detection/public/interfaces/textdetection.mojom',
   'mojo/public/js/bindings',
   'mojo/public/js/core',
   'content/public/renderer/interfaces',
  ], (textDetection, bindings, mojo, interfaces) => {

  class MockTextDetection {
    constructor() {
      this.bindingSet_ = new bindings.BindingSet(textDetection.TextDetection);

      interfaces.addInterfaceOverrideForTesting(
          textDetection.TextDetection.name,
          handle => this.bindingSet_.addBinding(this, handle));
    }

    detect(frame_data, width, height) {
      let receivedStruct = mojo.mapBuffer(frame_data, 0, width*height*4, 0);
      this.buffer_data_ = new Uint32Array(receivedStruct.buffer);
      return Promise.resolve({
        results: [
          {
            raw_value : "cats",
            bounding_box: { x: 1.0, y: 1.0, width: 100.0, height: 100.0 }
          },
          {
            raw_value : "dogs",
            bounding_box: { x: 2.0, y: 2.0, width: 50.0, height: 50.0 }
          },
        ],
      });
      mojo.unmapBuffer(receivedStruct.buffer);
    }

    getFrameData() {
      return this.buffer_data_;
    }
  }
  return new MockTextDetection();
});
