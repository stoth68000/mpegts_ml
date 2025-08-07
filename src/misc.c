/* TODO: Move this into the libltntstools once we're completely happy with it.
 * See ISO-14496-10:2004 section 7.3.1 NAL unit Syntax.
 */
static void ltn_nal_h264_strip_emulation_prevention(struct ltn_nal_headers_s *h)
{
        int dropped = 0;
        for (unsigned int i = 1; i < h->lengthBytes; i++) {
                if (i + 2 < h->lengthBytes &&
                        h->ptr[i + 0] == 0x00 &&
                        h->ptr[i + 1] == 0x00 &&
                        h->ptr[i + 2] == 0x03)
                {
                                /* Convert 00 00 03 to 00 00 */
                                memcpy((unsigned char *)&h->ptr[i + 2], &h->ptr[i + 3], h->lengthBytes - i - 3);
                                dropped++;
                }
        }
        h->lengthBytes -= dropped;
}

