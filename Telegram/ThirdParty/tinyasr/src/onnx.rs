//! Just enough of the ONNX format (protobuf) to take weights out of a model file: every tensor of the file
//! (initializers and Constant nodes, subgraphs included), by name.

use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::vec::Vec;

pub struct Tensor {
    pub data: Data,
}

pub enum Data {
    F32(Vec<f32>),
    U8(Vec<u8>),
    I8(Vec<i8>),
    I32(Vec<i32>),
    I64(Vec<i64>),
}

impl Tensor {
    /// The values as f32 (integers converted).
    pub fn f32s(&self) -> Vec<f32> {
        match &self.data {
            Data::F32(v) => v.clone(),
            Data::U8(v) => v.iter().map(|&x| x as f32).collect(),
            Data::I8(v) => v.iter().map(|&x| x as f32).collect(),
            Data::I32(v) => v.iter().map(|&x| x as f32).collect(),
            Data::I64(v) => v.iter().map(|&x| x as f32).collect(),
        }
    }
}

pub struct Graph {
    pub tensors: BTreeMap<String, Tensor>,
}

impl Graph {
    pub fn parse(b: &[u8]) -> Result<Graph, &'static str> {
        let mut g = Graph { tensors: BTreeMap::new() };
        let mut found = false;
        for f in Fields(b) {
            let (num, v) = f?;
            if num == 7 {
                g.graph(v.bytes()?, 0)?;
                found = true;
            }
        }
        if !found {
            return Err("not an ONNX model");
        }
        Ok(g)
    }

    pub fn tensor(&self, name: &str) -> Result<&Tensor, &'static str> {
        self.tensors.get(name).ok_or("ONNX model: a weight is missing")
    }

    /// `depth`: how deep in subgraphs (bounded, so a crafted file cannot exhaust the stack).
    fn graph(&mut self, b: &[u8], depth: usize) -> Result<(), &'static str> {
        if depth > 8 {
            return Err("ONNX model: subgraphs nested too deep");
        }
        for f in Fields(b) {
            let (num, v) = f?;
            match num {
                1 => self.node(v.bytes()?, depth)?,
                5 => {
                    let (name, t) = tensor(v.bytes()?)?;
                    self.tensors.insert(name, t);
                }
                _ => {}
            }
        }
        Ok(())
    }

    /// NodeProto (output 2, op_type 4, attribute 5): a Constant's value, and the tensors of subgraphs (If branches).
    fn node(&mut self, b: &[u8], depth: usize) -> Result<(), &'static str> {
        let (mut op, mut output, mut value) = (String::new(), None, None);
        for f in Fields(b) {
            let (num, v) = f?;
            match num {
                2 if output.is_none() => output = Some(v.string()?),
                4 => op = v.string()?,
                5 => {
                    // AttributeProto: t 5, g 6
                    for a in Fields(v.bytes()?) {
                        let (an, av) = a?;
                        match an {
                            5 => value = Some(tensor(av.bytes()?)?.1),
                            6 => self.graph(av.bytes()?, depth + 1)?,
                            _ => {}
                        }
                    }
                }
                _ => {}
            }
        }
        if let (true, Some(t), Some(o)) = (op == "Constant", value, output) {
            self.tensors.insert(o, t);
        }
        Ok(())
    }
}

/// TensorProto: dims 1, data_type 2, float_data 4, int32_data 5, int64_data 7, name 8, raw_data 9.
fn tensor(b: &[u8]) -> Result<(String, Tensor), &'static str> {
    let (mut dims, mut ty, mut name) = (Vec::new(), 0, String::new());
    let (mut raw, mut floats, mut i32s, mut i64s): (&[u8], Vec<f32>, Vec<i64>, Vec<i64>) = (&[], Vec::new(), Vec::new(), Vec::new());
    for f in Fields(b) {
        let (num, v) = f?;
        match num {
            1 => {
                let mut d = Vec::new();
                v.ints(&mut d)?;
                for x in d {
                    dims.push(usize::try_from(x).map_err(|_| "ONNX model: negative tensor size")?);
                }
            }
            2 => ty = v.int()?,
            4 => v.floats(&mut floats)?,
            5 => v.ints(&mut i32s)?,
            7 => v.ints(&mut i64s)?,
            8 => name = v.string()?,
            9 => raw = v.bytes()?,
            _ => {}
        }
    }
    let n = dims.iter().try_fold(1usize, |a, &d| a.checked_mul(d)).ok_or("ONNX model: tensor too large")?;
    let le4 = |c: &[u8]| [c[0], c[1], c[2], c[3]];
    let data = match ty {
        1 if !raw.is_empty() => Data::F32(raw.chunks_exact(4).map(|c| f32::from_le_bytes(le4(c))).collect()),
        1 => Data::F32(floats),
        2 if !raw.is_empty() => Data::U8(raw.to_vec()),
        2 => Data::U8(i32s.iter().map(|&x| x as u8).collect()),
        3 if !raw.is_empty() => Data::I8(raw.iter().map(|&x| x as i8).collect()),
        3 => Data::I8(i32s.iter().map(|&x| x as i8).collect()),
        6 if !raw.is_empty() => Data::I32(raw.chunks_exact(4).map(|c| i32::from_le_bytes(le4(c))).collect()),
        6 => Data::I32(i32s.iter().map(|&x| x as i32).collect()),
        7 if !raw.is_empty() => {
            Data::I64(raw.chunks_exact(8).map(|c| i64::from_le_bytes([c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]])).collect())
        }
        7 => Data::I64(i64s),
        // other types (bool, strings) are never weights here
        _ => Data::U8(Vec::new()),
    };
    let t = Tensor { data };
    let len = match &t.data {
        Data::F32(v) => v.len(),
        Data::U8(v) => v.len(),
        Data::I8(v) => v.len(),
        Data::I32(v) => v.len(),
        Data::I64(v) => v.len(),
    };
    if len != n && !matches!(ty, 9 | 8 | 0) {
        return Err("ONNX model: tensor size does not match its shape");
    }
    Ok((name, t))
}

// ---- protobuf wire format

enum Value<'a> {
    Varint(u64),
    Bytes(&'a [u8]),
    Fixed32(u32),
    Fixed64,
}

impl<'a> Value<'a> {
    fn bytes(&self) -> Result<&'a [u8], &'static str> {
        match self {
            Value::Bytes(b) => Ok(b),
            _ => Err("ONNX model: unexpected field type"),
        }
    }

    fn string(&self) -> Result<String, &'static str> {
        String::from_utf8(self.bytes()?.to_vec()).map_err(|_| "ONNX model: bad name")
    }

    fn int(&self) -> Result<i64, &'static str> {
        match self {
            Value::Varint(v) => Ok(*v as i64),
            _ => Err("ONNX model: unexpected field type"),
        }
    }

    /// A repeated integer field, packed or one value.
    fn ints(&self, out: &mut Vec<i64>) -> Result<(), &'static str> {
        match self {
            Value::Varint(v) => out.push(*v as i64),
            Value::Bytes(b) => {
                let mut p = 0;
                while p < b.len() {
                    out.push(varint(b, &mut p)? as i64);
                }
            }
            _ => return Err("ONNX model: unexpected field type"),
        }
        Ok(())
    }

    fn floats(&self, out: &mut Vec<f32>) -> Result<(), &'static str> {
        match self {
            Value::Fixed32(v) => out.push(f32::from_bits(*v)),
            Value::Bytes(b) => out.extend(b.chunks_exact(4).map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]]))),
            _ => return Err("ONNX model: unexpected field type"),
        }
        Ok(())
    }
}

fn varint(b: &[u8], p: &mut usize) -> Result<u64, &'static str> {
    let mut v = 0u64;
    for shift in (0..64).step_by(7) {
        let c = *b.get(*p).ok_or("ONNX model: truncated")?;
        *p += 1;
        v |= ((c & 0x7f) as u64) << shift;
        if c & 0x80 == 0 {
            return Ok(v);
        }
    }
    Err("ONNX model: bad varint")
}

/// The fields of one message: (field number, value).
struct Fields<'a>(&'a [u8]);

impl<'a> Iterator for Fields<'a> {
    type Item = Result<(u64, Value<'a>), &'static str>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.0.is_empty() {
            return None;
        }
        let r = (|| {
            let b = self.0;
            let mut p = 0;
            let key = varint(b, &mut p)?;
            let v = match key & 7 {
                0 => Value::Varint(varint(b, &mut p)?),
                1 => {
                    p = p.checked_add(8).filter(|&e| e <= b.len()).ok_or("ONNX model: truncated")?;
                    Value::Fixed64
                }
                2 => {
                    let n = usize::try_from(varint(b, &mut p)?).map_err(|_| "ONNX model: truncated")?;
                    let end = p.checked_add(n).filter(|&e| e <= b.len()).ok_or("ONNX model: truncated")?;
                    let s = &b[p..end];
                    p = end;
                    Value::Bytes(s)
                }
                5 => {
                    let c = b.get(p..p + 4).ok_or("ONNX model: truncated")?;
                    p += 4;
                    Value::Fixed32(u32::from_le_bytes([c[0], c[1], c[2], c[3]]))
                }
                _ => return Err("ONNX model: unsupported field type"),
            };
            if p > b.len() {
                return Err("ONNX model: truncated");
            }
            self.0 = &b[p..];
            Ok((key >> 3, v))
        })();
        if r.is_err() {
            self.0 = &[];
        }
        Some(r)
    }
}
