QHexView
========
QHexView is a hexadecimal widget for Qt5

<p align="center">
<img height="400" src="https://raw.githubusercontent.com/Dax89/QHexEdit/master/screenshots/QHexView.png"><br>
 <img src="https://img.shields.io/badge/license-MIT-8e725e.svg?style=flat-square">
 <a href="https://github.com/ellerbrock/open-source-badges/">
   <img src="https://badges.frapsoft.com/os/v1/open-source.png?v=103">
 </a>  
</p>
    
Features
-----
- Customizable data backend (see more below).
- Document/View based.
- Unlimited Undo/Redo.
- Fully Customizable.
- Fast rendering.
- Easy to Use.

Buffer Backends
-----
These are the available buffer backends:
- QMemoryBuffer: A simple, flat memory array.
- QMemoryRefBuffer: QHexView just display the referenced data, editing is disabled.<br>
<br>
It's also possible to create new data backends from scratch.

Usage
-----
The data is managed by QHexView through QHexDocument class.<br>
You can load a generic QIODevice using the method fromDevice() of QHexDocument class with various buffer backends.<br>
Helper methods are provided in order to load a QFile a In-Memory buffer in QHexView:<br>
```cpp
// Load data from In-Memory Buffer...
QHexDocument* document = QHexDocument::fromMemory<QMemoryBuffer>(bytearray);
// ...from a generic I/O device...
QHexDocument* document = QHexDocument::fromDevice<QMemoryBuffer>(iodevice);
/* ...or from File */
QHexDocument* document = QHexDocument::fromFile<QMemoryBuffer>("data.bin");

QHexView* hexview = new QHexView();
hexview->setDocument(document);                  // Associate QHexEditData with this QHexEdit

// Document editing
QByteArray data = document->read(24, 78);        // Read 78 bytes starting to offset 24
document->insert(4, "Hello QHexEdit");           // Insert a string to offset 4 
document->remove(6, 10);                         // Delete bytes from offset 6 to offset 10 
document->replace(30, "New Data");               // Replace bytes from offset 30 with the string "New Data"

// Metatadata management
QHexMetadata* hexmetadata = document->metadata();

hexmetadata->background(6, 0, 10, Qt::Red);      // Highlight background to line 6, from 0 to 10 
hexmetadata->foreground(8, 0, 15, Qt::darkBLue); // Highlight foreground to line 8, from 0 to 15
hexmetadata->comment(16, "I'm a comment!");      // Add a comment to line 16
hexmetadata->clear();                            // Reset styling
```

License
-----
QHexEdit is released under MIT license
